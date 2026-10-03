//-- cmd 패킷 프로토콜과 전송 (WebHID / Web Serial)
//
//   펌웨어 src/hw/driver/cmd.c 와 같은 포맷이다 (stm32h5-w6300 / NUCLEO-N657X0 과 같다).
//     STX0(0x02) STX1(0xFD) type cmd_l cmd_h err_l err_h len_l len_h [data] checksum
//     checksum = (~sum(header+data)) + 1
//
//   전송은 갈아끼울 수 있다. 펌웨어의 cmd 채널과 짝이 맞는다.
//     HidChannel    : USB HID (drv_hid.c). 64 B 리포트, [0] = 유효 바이트 수
//     SerialChannel : USB CDC (drv_usb.c). 921600 으로 연다 — 115200 이면 보드가 CDC 를 CLI 로 넘긴다
//
//   이 파일은 전송 / 프로토콜만 맡는다. 화면은 index.html 이 맡는다.

export const USB_VID          = 0x1209;     // pid.codes
export const USB_PID_BOOT     = 0xB563;
export const USB_PID_BOOT_MSC = 0xB564;
export const USB_PID_APP      = 0xB565;
export const USB_PIDS         = [USB_PID_BOOT, USB_PID_BOOT_MSC, USB_PID_APP];

export const isBootPid = (pid) => pid === USB_PID_BOOT || pid === USB_PID_BOOT_MSC;

export const PKT_TYPE_CMD  = 0x00;
export const PKT_TYPE_RESP = 0x01;

export const CDC_BAUD = 921600;

const RPT     = 64;
const PAYLOAD = RPT - 1;


export function buildPacket(cmd, data = new Uint8Array(0), type = PKT_TYPE_CMD) {
  const p  = new Uint8Array(9 + data.length + 1);
  const dv = new DataView(p.buffer);
  p[0] = 0x02; p[1] = 0xFD; p[2] = type;
  dv.setUint16(3, cmd, true);
  dv.setUint16(5, 0, true);
  dv.setUint16(7, data.length, true);
  p.set(data, 9);
  let sum = 0;
  for (let i = 0; i < 9 + data.length; i++) sum += p[i];
  p[9 + data.length] = ((~sum) + 1) & 0xFF;
  return p;
}

const hex4 = (v) => '0x' + v.toString(16).padStart(4, '0');


//-- 전송 공통. send() / takeRx() 만 채우면 된다.
export class Channel {
  constructor() { this._rx = new Uint8Array(0); }

  async send(bytes) { throw new Error('not implemented'); }
  takeRx()          { throw new Error('not implemented'); }
  async close()     {}

  async request(cmd, data, timeoutMs = 3000) {
    this.takeRx();                       // 앞서 남은 바이트를 버린다
    this._rx = new Uint8Array(0);
    await this.send(buildPacket(cmd, data || new Uint8Array(0)));

    const t0 = performance.now();
    for (;;) {
      this._append(this.takeRx());

      // STX 를 찾아 정렬한다. 응답(type RESP)이고 cmd 가 같을 때만 받는다.
      // 115200 으로 연 CDC 에서는 CLI 가 입력을 에코하는데, 그 에코도 02 FD 로 시작한다 (cmdproto.py 와 같은 이유).
      for (let i = 0; i + 9 <= this._rx.length; i++) {
        if (this._rx[i] !== 0x02 || this._rx[i + 1] !== 0xFD) continue;
        const dv   = new DataView(this._rx.buffer, this._rx.byteOffset + i);
        const type = this._rx[i + 2];
        const rcmd = dv.getUint16(3, true);
        const err  = dv.getUint16(5, true);
        const len  = dv.getUint16(7, true);
        if (type !== PKT_TYPE_RESP || rcmd !== cmd) continue;
        if (this._rx.length - i < 9 + len + 1) break;
        return { err, data: this._rx.slice(i + 9, i + 9 + len) };
      }

      if (performance.now() - t0 > timeoutMs)
        throw new Error(`cmd ${hex4(cmd)} 응답 없음`);
      await new Promise(r => setTimeout(r, 1));
    }
  }

  _append(chunk) {
    if (!chunk || !chunk.length) return;
    const n = new Uint8Array(this._rx.length + chunk.length);
    n.set(this._rx);
    n.set(chunk, this._rx.length);
    this._rx = n;
  }
}


//-- USB HID. 리포트 [0] = 유효 바이트 수, [1:] = 페이로드 (drv_hid.c 와 같은 규약)
export class HidChannel extends Channel {
  constructor(device) {
    super();
    this.dev     = device;
    this.kind    = 'HID';
    this.pid     = device.productId;
    this.pending = [];
    this._onReport = (e) => {
      const d = new Uint8Array(e.data.buffer, e.data.byteOffset, e.data.byteLength);
      const n = d[0];
      if (n > 0 && n <= PAYLOAD) this.pending.push(d.slice(1, 1 + n));
    };
    device.addEventListener('inputreport', this._onReport);
  }

  static async open(device) {
    if (!device.opened) await device.open();
    return new HidChannel(device);
  }

  takeRx() {
    if (!this.pending.length) return new Uint8Array(0);
    const total = this.pending.reduce((a, b) => a + b.length, 0);
    const out = new Uint8Array(total);
    let o = 0;
    for (const p of this.pending) { out.set(p, o); o += p.length; }
    this.pending = [];
    return out;
  }

  async send(bytes) {
    for (let i = 0; i < bytes.length; i += PAYLOAD) {
      const c   = bytes.slice(i, i + PAYLOAD);
      const rpt = new Uint8Array(RPT);
      rpt[0] = c.length;
      rpt.set(c, 1);
      await this.dev.sendReport(0, rpt);
    }
  }

  async close() {
    this.dev.removeEventListener('inputreport', this._onReport);
    try { if (this.dev.opened) await this.dev.close(); } catch (e) { /* 이미 빠졌다 */ }
  }
}


//-- USB CDC (Web Serial). 921600 으로 연다.
export class SerialChannel extends Channel {
  constructor(port) {
    super();
    this.port    = port;
    this.kind    = 'CDC';
    this.pid     = port.getInfo().usbProductId;
    this.pending = [];
    this.reader  = null;
    this.writer  = null;
    this.running = false;
  }

  static async open(port) {
    const ch = new SerialChannel(port);
    await port.open({ baudRate: CDC_BAUD, bufferSize: 4096 });
    ch.writer  = port.writable.getWriter();
    ch.running = true;
    ch._loop = ch._readLoop();
    return ch;
  }

  async _readLoop() {
    while (this.running && this.port.readable) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value && value.length) this.pending.push(value);
        }
      } catch (e) {
        // 보드가 리셋해 USB 가 빠졌다. 루프를 끝낸다.
        this.running = false;
      } finally {
        try { this.reader.releaseLock(); } catch (e) { /* */ }
      }
    }
  }

  takeRx() {
    if (!this.pending.length) return new Uint8Array(0);
    const total = this.pending.reduce((a, b) => a + b.length, 0);
    const out = new Uint8Array(total);
    let o = 0;
    for (const p of this.pending) { out.set(p, o); o += p.length; }
    this.pending = [];
    return out;
  }

  async send(bytes) {
    await this.writer.write(bytes);
  }

  async close() {
    this.running = false;
    try { if (this.reader) await this.reader.cancel(); } catch (e) { /* */ }
    try { if (this.writer) { this.writer.releaseLock(); } } catch (e) { /* */ }
    try { await this.port.close(); } catch (e) { /* 이미 빠졌다 */ }
  }
}


//-- 장치 고르기 / 다시 찾기
//
//   부트로더(B563)와 앱(B565)은 PID 가 다르다. 브라우저 권한은 장치(VID/PID)마다라서
//   앱에 붙어 업데이트를 시작하면, 부트로더로 바뀐 장치는 **처음 한 번 사용자가 다시 골라야** 한다.
//   한 번 고르면 브라우저가 기억해 다음부터는 getDevices() / getPorts() 로 바로 찾는다.

const hidFilters    = (pids) => pids.map(p => ({ vendorId: USB_VID, productId: p }));
const serialFilters = (pids) => pids.map(p => ({ usbVendorId: USB_VID, usbProductId: p }));

export async function requestChannel(kind, pids = USB_PIDS) {
  if (kind === 'HID') {
    const devs = await navigator.hid.requestDevice({ filters: hidFilters(pids) });
    if (!devs.length) return null;
    return HidChannel.open(devs[0]);
  }
  const port = await navigator.serial.requestPort({ filters: serialFilters(pids) });
  return SerialChannel.open(port);
}

// 이미 권한이 있는 장치 중 pids 에 맞는 것을 찾아 연다. 없으면 null.
export async function findChannel(kind, pids) {
  if (kind === 'HID') {
    const devs = (await navigator.hid.getDevices())
      .filter(d => d.vendorId === USB_VID && pids.includes(d.productId));
    return devs.length ? HidChannel.open(devs[0]) : null;
  }
  const ports = (await navigator.serial.getPorts())
    .filter(p => { const i = p.getInfo(); return i.usbVendorId === USB_VID && pids.includes(i.usbProductId); });
  for (const p of ports) {
    try { return await SerialChannel.open(p); } catch (e) { /* 다른 프로그램이 쥐고 있거나 아직 준비 안 됨 */ }
  }
  return null;
}

// 보드가 리셋한 뒤 pids 장치가 나타날 때까지 기다린다. 권한이 없으면 timeoutMs 뒤 null.
export async function waitChannel(kind, pids, timeoutMs = 6000) {
  const t0 = performance.now();
  await new Promise(r => setTimeout(r, 500));
  while (performance.now() - t0 < timeoutMs) {
    try {
      const ch = await findChannel(kind, pids);
      if (ch) {
        await new Promise(r => setTimeout(r, 300));
        return ch;
      }
    } catch (e) { /* 열거 중 */ }
    await new Promise(r => setTimeout(r, 200));
  }
  return null;
}

export const str32 = (u8, off) =>
  new TextDecoder().decode(u8.slice(off, off + 32)).split('\0')[0].trim();
