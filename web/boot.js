//-- 부트로더 커맨드 셋과 앱(펌웨어) 다운로드
//
//   펌웨어 src/ap/modules/cmd/process/cmd_boot.c 와 맞는다. 흐름은 tools/download.py 와 같다.
//     INFO → (앱이면 FW_UPDATE 로 부트로더에 넘기고 다시 붙는다) → BEGIN → ERASE → WRITE … → END → VERIFY → JUMP

import { str32, BOOT_PIDS, waitChannel } from './proto.js?v=20261003-5';

export const BOOT_CMD = {
  INFO:      0x0000,
  VERSION:   0x0001,
  FW_BEGIN:  0x0002,
  FW_ERASE:  0x0003,
  FW_WRITE:  0x0004,
  FW_READ:   0x0005,
  FW_END:    0x0006,
  FW_VERIFY: 0x0007,
  FW_UPDATE: 0x0008,
  FW_JUMP:   0x0009,
  BAUD:      0x0020,
  RESET:     0x0021,
};

export const DEV_MODE_BOOT = 0;
export const DEV_MODE_APP  = 1;

export const TARGET_FW = 0;
export const CHUNK     = 1008;      // 1024 B 데이터 - offset 4 B 안에서 16 B(쿼드워드) 배수

const IMG_TYPE = ['NONE', 'RAW', 'VER', 'TAG'];

export const ERR_STR = {
  0x000A: 'WRONG_CMD', 0x000C: 'FLASH_ERASE', 0x000D: 'WRONG_RANGE', 0x000E: 'FLASH_WRITE',
  0x0010: 'INVALID_FW', 0x0011: 'FW_CRC', 0x0012: 'JUMP_TO_FW', 0x0015: 'FLASH_READ',
  0x0020: 'CMD_MAX_LENGTH', 0x0021: 'CMD_CHECKSUM',
};
export const errStr = (e) => `0x${e.toString(16).padStart(4, '0')} (${ERR_STR[e] || '?'})`;


//-- INFO 응답 (boot_info_t, packed). 앞 104 B + 확장 20 B
export function parseInfo(d) {
  if (d.length < 104) throw new Error(`INFO 응답이 짧다 (${d.length} B)`);
  const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
  const u  = (o) => dv.getUint32(o, true);
  const info = {
    magic: u(0), mode: u(4), bootAddr: u(8), bootSize: u(12), firmAddr: u(16), firmVecAddr: u(20),
    firmSize: u(24), tagSize: u(28), maxFwSize: u(32), familyId: u(36),
    name: str32(d, 40), version: str32(d, 72),
    cmdVer: 0, baud: 0,
  };
  if (d.length >= 124) { info.cmdVer = u(104); info.baud = u(120); }
  info.modeStr = info.mode === DEV_MODE_BOOT ? 'BOOT' : 'APP';
  return info;
}

//-- VERSION 응답 (boot_version_t) : img u8, rsv[3], fw_size u32, fw_crc u32, name[32], version[32]
export function parseVersion(d) {
  if (d.length < 76) throw new Error(`VERSION 응답이 짧다 (${d.length} B)`);
  const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
  return {
    img: d[0], imgStr: IMG_TYPE[d[0]] || '?',
    size: dv.getUint32(4, true), crc: dv.getUint32(8, true),
    name: str32(d, 12), version: str32(d, 44),
  };
}

//-- 펌웨어 util_core.c 의 utilCalcCRC 와 같다 (CRC-16, poly 0x8005, MSB first, 초기값 0)
const CRC_TABLE = (() => {
  const t = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i << 8;
    for (let j = 0; j < 8; j++) c = (c & 0x8000) ? ((c << 1) ^ 0x8005) : (c << 1);
    t[i] = c & 0xFFFF;
  }
  return t;
})();

export function crc16(data, crc = 0) {
  for (let i = 0; i < data.length; i++)
    crc = ((crc << 8) ^ CRC_TABLE[((crc >> 8) ^ data[i]) & 0xFF]) & 0xFFFF;
  return crc;
}

//-- 이미지 안의 firm_ver_t (벡터 1 KB 뒤) : magic, version[32], name[32], firm_addr, firm_size
export function readImageVer(img) {
  const VER = 0x400;
  if (img.length < VER + 76) return null;
  const dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
  if (dv.getUint32(VER, true) !== 0x56455220) return null;
  return {
    version: str32(img, VER + 4), name: str32(img, VER + 36),
    addr: dv.getUint32(VER + 68, true), size: dv.getUint32(VER + 72, true),
  };
}

async function requestOk(ch, cmd, data, timeoutMs, what) {
  const r = await ch.request(cmd, data, timeoutMs);
  if (r.err) throw new Error(`${what} 실패 err=${errStr(r.err)}`);
  return r;
}


/*
 * 앱(펌웨어) 다운로드.
 *
 *   ch        : 열린 채널 (HidChannel / SerialChannel)
 *   image     : Uint8Array (stm32h5-fw.bin)
 *   ui        : { log(msg), progress(0..1), askBoot() }
 *               askBoot() 는 부트로더 장치를 사용자가 고르게 한다 (권한이 없을 때). 채널을 돌려준다
 *
 *   돌려주는 것 : 마지막에 쓴 채널 (부트로더 → 앱으로 점프한 뒤라 이미 끊겨 있다)
 */
export async function downloadFirmware(ch, image, ui) {
  const kind = ch.kind;
  let info = parseInfo((await requestOk(ch, BOOT_CMD.INFO, null, 2000, 'INFO')).data);
  ui.log(`연결 : ${info.name} ${info.version} [${info.modeStr}]`);

  if (info.mode !== DEV_MODE_BOOT) {
    // 앱이 돌고 있다. 부트로더에 머물러 달라고 하고(FW_UPDATE → resetToBoot) 다시 붙는다
    ui.log('앱이 실행 중 → 부트로더로 넘어가 다시 붙는다');
    const appPid = ch.pid;
    try { await ch.request(BOOT_CMD.FW_UPDATE, null, 1000); } catch (e) { /* 리셋하면서 끊긴다 */ }
    await ch.close();

    // 1) 부트로더 PID 가 달라 권한이 없을 것 같으면 선택창을 곧바로 연다 (클릭 직후라 브라우저가 허용한다).
    //    선택창은 장치가 나타나면 목록을 갱신하므로, 부트로더가 다시 열거되는 순간 목록에 뜬다.
    // 2) 권한이 있으면 그 장치가 나타날 때까지 기다린다.
    // 3) 둘 다 안 되면 사용자가 버튼으로 고르게 한다.
    //    앱의 PID 가 부트로더 PID 이기도 하면 (지금처럼 같은 B563) 권한이 그대로라 선택창이 필요 없다.
    const samePid = BOOT_PIDS.includes(appPid);
    ch = (!samePid && ui.autoAskBoot) ? await ui.autoAskBoot() : null;
    if (!ch) ch = await waitChannel(kind, BOOT_PIDS);
    if (!ch) {
      ui.log('부트로더 장치 권한이 없다 (처음 한 번). 펌웨어 카드의 [부트로더 고르기] 를 눌러 STM32H5-BOOT 를 골라 줄 것');
      ch = await ui.askBoot();
      if (!ch) throw new Error('부트로더에 다시 붙지 못했다');
    }
    if (ui.bootGranted) ui.bootGranted();
    info = parseInfo((await requestOk(ch, BOOT_CMD.INFO, null, 3000, 'INFO')).data);
    ui.log(`연결 : ${info.name} ${info.version} [${info.modeStr}]`);
    if (info.mode !== DEV_MODE_BOOT) throw new Error('부트로더로 넘어가지 않았다');
  }
  if (info.cmdVer === 0) throw new Error('확장 INFO 가 없다 (다른 보드의 부트로더?)');
  if (image.length > info.maxFwSize) throw new Error(`이미지가 크다 (${image.length} > ${info.maxFwSize} B)`);

  const v = parseVersion((await requestOk(ch, BOOT_CMD.VERSION, null, 2000, 'VERSION')).data);
  ui.log(v.img ? `현재 FW : ${v.name} ${v.version} [${v.imgStr}] ${v.size} B crc 0x${v.crc.toString(16).toUpperCase()}`
               : '현재 FW : 없음');

  const begin = new Uint8Array(5);
  new DataView(begin.buffer).setUint32(0, image.length, true);
  begin[4] = TARGET_FW;
  await requestOk(ch, BOOT_CMD.FW_BEGIN, begin, 2000, 'BEGIN');

  let t0 = performance.now();
  await requestOk(ch, BOOT_CMD.FW_ERASE, null, 30000, 'ERASE');
  ui.log(`지우기 ${((performance.now() - t0) / 1000).toFixed(2)} s`);

  t0 = performance.now();
  for (let off = 0; off < image.length; off += CHUNK) {
    const part = image.slice(off, off + CHUNK);
    const pkt  = new Uint8Array(4 + part.length);
    new DataView(pkt.buffer).setUint32(0, off, true);
    pkt.set(part, 4);
    const r = await ch.request(BOOT_CMD.FW_WRITE, pkt, 3000);
    if (r.err) throw new Error(`WRITE 0x${off.toString(16)} 실패 err=${errStr(r.err)}`);
    ui.progress(Math.min(1, (off + part.length) / image.length));
  }
  const dt = (performance.now() - t0) / 1000;
  ui.log(`쓰기 ${dt.toFixed(2)} s (${(image.length / dt / 1024).toFixed(1)} KB/s)`);

  const end = await requestOk(ch, BOOT_CMD.FW_END, null, 30000, 'END');
  const edv = new DataView(end.data.buffer, end.data.byteOffset, end.data.byteLength);
  const size = edv.getUint32(0, true), crc = edv.getUint32(4, true);
  const host = crc16(image.subarray(0, size));
  ui.log(`확인 ${size} B crc 0x${crc.toString(16).toUpperCase()} (호스트 0x${host.toString(16).toUpperCase()}) ${crc === host ? 'OK' : 'FAIL'}`);
  if (crc !== host) throw new Error('CRC 가 다르다');

  const vr = await ch.request(BOOT_CMD.FW_VERIFY, null, 10000);
  ui.log(`판정 ${IMG_TYPE[vr.data[0]] || '?'}`);

  const j = await ch.request(BOOT_CMD.FW_JUMP, null, 2000);
  ui.log(j.err ? `실행 실패 err=${errStr(j.err)}` : '실행 : 앱으로 점프');
  await ch.close();          // 앱으로 점프하며 USB 가 다시 열거된다
}
