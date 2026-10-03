//-- STM32 ROM DFU (DfuSe) — WebUSB
//
//   보드의 BOOT0 버튼(S3)을 누른 채 리셋(S2)하면 STM32H5 의 시스템 부트로더(ROM)가 USB DFU 로 열거된다.
//   VID/PID 0483:DF11. 여기에 우리 부트로더(stm32h5-boot.bin)를 0x08000000 에 쓴다.
//
//   DfuSe 는 표준 DFU 에 ST 확장 명령을 얹은 것이다 (UM0424 / AN3156).
//     DNLOAD wBlockNum 0 : 명령.  [0x21, addr]  주소 지정,  [0x41, addr]  그 주소의 섹터 지우기
//     DNLOAD wBlockNum ≥2 : 데이터. 주소 = 지정한 주소 + (wBlockNum - 2) × transferSize
//     매 DNLOAD 뒤 GET_STATUS 로 끝날 때까지 기다린다 (bwPollTimeout)
//   절차는 dfu-util 의 DfuSe 처리와 같다.

const ST_VID = 0x0483;
const ST_DFU_PID = 0xDF11;

const DFU_DNLOAD    = 1;
const DFU_UPLOAD    = 2;
const DFU_GETSTATUS = 3;
const DFU_CLRSTATUS = 4;
const DFU_ABORT     = 6;

const STATE = {
  appIDLE: 0, appDETACH: 1, dfuIDLE: 2, dfuDNLOAD_SYNC: 3, dfuDNBUSY: 4, dfuDNLOAD_IDLE: 5,
  dfuMANIFEST_SYNC: 6, dfuMANIFEST: 7, dfuMANIFEST_WAIT_RESET: 8, dfuUPLOAD_IDLE: 9, dfuERROR: 10,
};

const sleep = (ms) => new Promise(r => setTimeout(r, ms));


//-- 인터페이스 이름의 메모리 배치를 읽는다.  예) "@Internal Flash  /0x08000000/0256*008Kg"
//   세그먼트마다 섹터 개수 × 크기(B/K/M) + 속성(a~g, g = 읽기·지우기·쓰기)
export function parseMemoryLayout(name) {
  const m = /^@([^/]+)\/(0x[0-9a-fA-F]+)\/(.+)$/.exec((name || '').trim());
  if (!m) return null;
  let addr = parseInt(m[2], 16);
  const segs = [];
  for (const part of m[3].split(',')) {
    const s = /(\d+)\*\s*(\d+)\s*([BKM ]?)([a-g])/.exec(part.trim());
    if (!s) continue;
    const mul  = { 'B': 1, ' ': 1, '': 1, 'K': 1024, 'M': 1024 * 1024 }[s[3]];
    const size = parseInt(s[2], 10) * mul;
    const n    = parseInt(s[1], 10);
    segs.push({ start: addr, sectorSize: size, sectors: n, end: addr + n * size, erasable: 'cdfg'.includes(s[4]) });
    addr += n * size;
  }
  return { name: m[1].trim(), segs };
}


export class DfuDevice {
  constructor(dev) {
    this.dev = dev;
    this.intf = 0;
    this.alt = 0;
    this.altName = '';
    this.layout = null;
    this.transferSize = 1024;
  }

  static async request() {
    const dev = await navigator.usb.requestDevice({ filters: [{ vendorId: ST_VID, productId: ST_DFU_PID }] });
    const d = new DfuDevice(dev);
    await d.open();
    return d;
  }

  async open() {
    const dev = this.dev;
    await dev.open();
    if (dev.configuration === null) await dev.selectConfiguration(1);

    // DFU 인터페이스(class 0xFE / subclass 0x01) 중 내장 플래시 대체 설정을 고른다
    let pick = null;
    for (const itf of dev.configuration.interfaces) {
      for (const a of itf.alternates) {
        if (a.interfaceClass !== 0xFE || a.interfaceSubclass !== 0x01) continue;
        const name = a.interfaceName || '';
        if (!pick || /internal flash/i.test(name)) {
          pick = { intf: itf.interfaceNumber, alt: a.alternateSetting, name };
          if (/internal flash/i.test(name)) break;
        }
      }
      if (pick && /internal flash/i.test(pick.name)) break;
    }
    if (!pick) throw new Error('DFU 인터페이스를 찾지 못했다');

    this.intf = pick.intf;
    this.alt = pick.alt;

    await dev.claimInterface(this.intf);
    await dev.selectAlternateInterface(this.intf, this.alt);

    // 구성 디스크립터에서 전송 크기와 (WebUSB 가 이름을 안 줄 때) 인터페이스 이름을 읽는다
    const desc = await this._readConfigDesc();
    this.transferSize = desc.transferSize;
    this.altName = pick.name || desc.altName || '';
    this.layout = parseMemoryLayout(this.altName);
  }

  /*
   * 구성 디스크립터를 직접 읽는다.
   *   DFU 기능 디스크립터(type 0x21) 의 wTransferSize
   *   이 인터페이스 / 대체 설정의 인터페이스 디스크립터(type 0x04) 의 iInterface → 문자열 디스크립터 = 메모리 배치
   *
   * WebUSB 의 alternate.interfaceName 은 비어 올 수 있다 (macOS Chrome 실측 : "(이름 없음)").
   * 그러면 메모리 배치를 못 읽어 8 KB 섹터로 가정하게 되므로, 문자열을 직접 가져온다.
   */
  async _readConfigDesc() {
    const out = { transferSize: 1024, altName: '' };
    let iInterface = 0;
    try {
      const r = await this.dev.controlTransferIn(
        { requestType: 'standard', recipient: 'device', request: 6, value: 0x0200, index: 0 }, 1024);
      const d = new Uint8Array(r.data.buffer);
      let cur = null;
      for (let i = 0; i + 1 < d.length && d[i] > 0; i += d[i]) {
        const type = d[i + 1];
        if (type === 0x04 && d[i] >= 9) cur = { num: d[i + 2], alt: d[i + 3], str: d[i + 8] };
        if (type === 0x04 && cur && cur.num === this.intf && cur.alt === this.alt) iInterface = cur.str;
        if (type === 0x21 && d[i] >= 7) out.transferSize = d[i + 5] | (d[i + 6] << 8);
      }
    } catch (e) { /* 기본값 */ }

    if (iInterface) {
      try {
        const r = await this.dev.controlTransferIn(
          { requestType: 'standard', recipient: 'device', request: 6, value: 0x0300 | iInterface, index: 0x0409 }, 255);
        const d = new Uint8Array(r.data.buffer);
        let str = '';
        for (let i = 2; i + 1 < d[0] && i + 1 < d.length; i += 2) str += String.fromCharCode(d[i] | (d[i + 1] << 8));
        out.altName = str;
      } catch (e) { /* 이름 없이 간다 (8 KB 섹터 가정) */ }
    }
    return out;
  }

  async close() {
    try { await this.dev.releaseInterface(this.intf); } catch (e) { /* */ }
    try { await this.dev.close(); } catch (e) { /* */ }
  }

  _out(request, value, data) {
    return this.dev.controlTransferOut(
      { requestType: 'class', recipient: 'interface', request, value, index: this.intf }, data);
  }

  _in(request, value, length) {
    return this.dev.controlTransferIn(
      { requestType: 'class', recipient: 'interface', request, value, index: this.intf }, length);
  }

  async getStatus() {
    const r = await this._in(DFU_GETSTATUS, 0, 6);
    const d = new Uint8Array(r.data.buffer);
    return { status: d[0], pollTimeout: d[1] | (d[2] << 8) | (d[3] << 16), state: d[4] };
  }

  async clearStatus() { await this._out(DFU_CLRSTATUS, 0); }
  async abort()       { await this._out(DFU_ABORT, 0); }

  // 오류 / 중간 상태를 걷어 dfuIDLE 로 만든다
  async toIdle() {
    let s = await this.getStatus();
    if (s.state === STATE.dfuERROR) { await this.clearStatus(); s = await this.getStatus(); }
    if (s.state !== STATE.dfuIDLE) { await this.abort(); s = await this.getStatus(); }
    if (s.state !== STATE.dfuIDLE) throw new Error(`dfuIDLE 로 가지 않는다 (state ${s.state})`);
  }

  // DNLOAD 한 번 하고 끝날 때까지 GET_STATUS 를 돈다
  async _dnload(block, data) {
    await this._out(DFU_DNLOAD, block, data);
    for (;;) {
      const s = await this.getStatus();
      if (s.status !== 0) throw new Error(`DFU 오류 status ${s.status}, state ${s.state}`);
      if (s.state === STATE.dfuDNLOAD_IDLE || s.state === STATE.dfuIDLE) return s;
      if (s.state === STATE.dfuMANIFEST || s.state === STATE.dfuMANIFEST_WAIT_RESET) return s;
      await sleep(Math.max(s.pollTimeout, 1));
    }
  }

  static _cmd(op, addr) {
    const b = new Uint8Array(5);
    b[0] = op;
    new DataView(b.buffer).setUint32(1, addr >>> 0, true);
    return b;
  }

  setAddress(addr)  { return this._dnload(0, DfuDevice._cmd(0x21, addr)); }
  eraseSector(addr) { return this._dnload(0, DfuDevice._cmd(0x41, addr)); }

  // [addr, addr+len) 을 덮는 섹터들의 시작 주소
  sectorsFor(addr, len) {
    const out = [];
    const end = addr + len;
    const segs = this.layout ? this.layout.segs : [{ start: 0x08000000, sectorSize: 8192, sectors: 256, end: 0x08200000 }];
    for (const s of segs) {
      for (let a = s.start; a < s.end; a += s.sectorSize) {
        if (a + s.sectorSize > addr && a < end) out.push(a);
      }
    }
    return out;
  }

  /*
   * image 를 addr 에 쓴다.
   *   ui : { log(msg), progress(0..1, what) }
   *   verify : 다 쓴 뒤 다시 읽어 비교한다 (UPLOAD)
   */
  async write(addr, image, ui, verify = true) {
    await this.toIdle();

    const sectors = this.sectorsFor(addr, image.length);
    if (!sectors.length) throw new Error('지울 섹터를 찾지 못했다 (메모리 배치?)');
    ui.log(`DFU : ${this.altName || '(이름 없음)'}, 전송 ${this.transferSize} B`);

    let t0 = performance.now();
    for (let i = 0; i < sectors.length; i++) {
      await this.eraseSector(sectors[i]);
      ui.progress((i + 1) / sectors.length, '지우기');
    }
    ui.log(`지우기 ${sectors.length} 섹터, ${((performance.now() - t0) / 1000).toFixed(2)} s`);

    t0 = performance.now();
    await this.setAddress(addr);
    const ts = this.transferSize;
    for (let off = 0, blk = 2; off < image.length; off += ts, blk++) {
      await this._dnload(blk, image.slice(off, off + ts));
      ui.progress(Math.min(1, (off + ts) / image.length), '쓰기');
    }
    const dt = (performance.now() - t0) / 1000;
    ui.log(`쓰기 ${image.length} B, ${dt.toFixed(2)} s (${(image.length / dt / 1024).toFixed(1)} KB/s)`);

    if (verify) {
      t0 = performance.now();
      await this.toIdle();
      await this.setAddress(addr);
      await this.abort();                          // 주소 지정 뒤 dfuIDLE 로 돌아가야 UPLOAD 할 수 있다
      const back = new Uint8Array(image.length);
      for (let off = 0, blk = 2; off < image.length; off += ts, blk++) {
        const n = Math.min(ts, image.length - off);
        const r = await this._in(DFU_UPLOAD, blk, n);
        back.set(new Uint8Array(r.data.buffer).subarray(0, n), off);
        ui.progress(Math.min(1, (off + n) / image.length), '확인');
      }
      await this.abort();
      for (let i = 0; i < image.length; i++) {
        if (back[i] !== image[i]) throw new Error(`확인 실패 : 0x${(addr + i).toString(16)} 에서 다르다`);
      }
      ui.log(`확인 OK, ${((performance.now() - t0) / 1000).toFixed(2)} s`);
    }
  }

  /*
   * 쓰기를 마무리한다. **leave(go) 는 하지 않는다.**
   *
   * DfuSe 의 leave (주소 지정 → 길이 0 DNLOAD → GET_STATUS, dfu-util 의 :leave / CubeProgrammer 의 -g) 를 보내면
   * STM32H563 ROM 은 0x08000000 으로 점프하다 **코어 락업**에 빠진다 (2026-10-03 실측, CubeProgrammer -g 0x08000000).
   *   PC 0xEFFFFFFE (lockup), MSP 0x3004xxxx · VTOR 0x0FF80300 (ROM 것 그대로), CFSR 0x1001 (IACCVIOL + STKERR), HFSR FORCED
   * 우리 코드는 한 줄도 돌지 않았다. ROM 이 켜 둔 MPU 가 남은 채 플래시를 실행하려다 막힌 것으로 본다.
   * 어차피 리셋이 필요하므로, 락업보다 상태가 분명한 "DFU 장치로 남아 있기" 로 끝내고 사용자에게 리셋을 부탁한다.
   */
  async finish() {
    try { await this.toIdle(); } catch (e) { /* */ }
  }
}
