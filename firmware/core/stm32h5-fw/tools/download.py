#!/usr/bin/env python3
"""UART(ST-LINK VCP) / USB CDC / USB HID 로 앱을 내려받는다. (macOS / Linux / Windows)

  python3 tools/download.py [bin] [--via uart|cdc|hid] [--port P] [--baud B] [--no-reset]

  --via uart (기본) : ST-LINK VCP. CLI 와 같은 포트이고 보드가 cmd 패킷만 골라낸다. --baud 로 보율을 올린다
  --via cdc         : 보드의 USB CDC. 921600 으로 연다 (115200 이면 보드가 CLI 로 넘긴다). 터미널이 그 포트를 열고 있으면 못 연다
  --via hid         : 보드의 USB HID. 터미널이 CDC 를 CLI 로 쓰는 중에도 된다 (hidapi 필요)

  앱 (기본 bin: build/stm32h5-fw.bin) 을 부트로더가 FIRM 영역에 쓰고, 다 쓴 뒤
  TAG 를 기록하고 (커밋) 앱으로 점프한다 (--no-reset 으로 끈다).
  앱이 돌고 있으면 부트로더로 넘긴 뒤 쓴다.

  stm32n6-boot/tools/download.py 를 바탕으로 했다. 이 보드는 단순형 레이아웃이라 대상이 앱(FW) 하나다.

  cmd 패킷은 CLI 와 같은 UART 로 간다. 보드는 02 FD 로 시작하는 패킷만 골라낸다.
  115200 으로 붙어서 --baud 로 올리고 (기본 4 Mbps, 0 이면 그대로), 끝나면 115200 으로 되돌린다.
  baram-term 이 포트를 열고 있으면 baram-ctl 로 잠시 놓게 한다 (--no-baram 으로 끈다).

  필요 : pyserial
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import *   # noqa: E402,F403

PRJ_DIR   = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEF_FW    = os.path.join(PRJ_DIR, "build", "stm32h5-fw.bin")
BAUD_DEF  = 115200
CDC_BAUD  = 921600               # 115200 이 아니면 된다. 115200 으로 열면 보드가 CDC 를 CLI 에 넘긴다
CHUNK     = 1008                 # cmd 데이터 최대 1024 B - offset 4 B 안에서, 16 B(쿼드워드) 배수여야 한다


def find_cdc_port():
  from serial.tools import list_ports
  ports = [p.device for p in list_ports.comports() if p.vid == USB_VID and p.pid in USB_PIDS]
  return sorted(ports)[0] if ports else None


class Link:
  """연결 방식마다 열기 / 닫기 / (리셋 뒤) 다시 열기. UART 는 리셋해도 포트가 남고,
     CDC / HID 는 보드가 리셋하면 USB 가 다시 열거되므로 다시 찾아 연다."""

  def __init__(self, via, port):
    self.via  = via
    self.port = port
    self.tr   = None

  def open(self):
    import serial
    if self.via == "hid":
      self.tr   = HidTransport()
      self.name = f"USB HID {self.tr.vid:04X}:{self.tr.pid:04X}"
    elif self.via == "cdc":
      self.port = self.port or find_cdc_port()
      if not self.port:
        raise OSError("USB CDC 포트를 찾지 못했다")
      self.tr   = SerialTransport(serial.Serial(self.port, CDC_BAUD, timeout=0.2))
      self.name = f"{self.port} (USB CDC)"
    else:
      self.tr   = SerialTransport(serial.Serial(self.port, BAUD_DEF, timeout=0.2))
      self.name = self.port
    return self.tr

  def close(self):
    if self.tr:
      self.tr.close()
      self.tr = None

  def reopen(self, timeout=6.0):
    """보드가 리셋한 뒤 다시 붙는다. USB 는 사라졌다 다시 나타날 때까지 기다린다."""
    if self.via == "uart":
      time.sleep(1.5)
      return self.tr
    self.close()
    time.sleep(0.5)
    if self.via == "cdc":
      self.port = None
    t0 = time.time()
    while True:
      try:
        self.open()
        time.sleep(0.3)
        return self.tr
      except (OSError, IOError) as e:
        if time.time() - t0 > timeout:
          raise OSError(f"다시 붙지 못했다: {e}")
        time.sleep(0.2)


def find_port():
  try:
    from serial.tools import list_ports
    # ST VID(0x0483) 는 ST 칩을 쓴 다른 장치도 쓴다. ST-LINK 의 PID / 이름으로 고른다
    stlink_pid = {0x374B, 0x374E, 0x374F, 0x3752, 0x3753, 0x3754, 0x3757}
    st = [p.device for p in list_ports.comports()
          if p.vid == 0x0483 and (p.pid in stlink_pid or "STLINK" in (p.product or "").upper())]
    if st:
      return sorted(st)[0]
  except ImportError:
    pass
  ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
  return ports[0] if ports else None


class BaramTerm:
  """baram-term 이 포트를 쥐고 있으면 놓게 하고, 끝나면 다시 열게 한다."""

  def __init__(self, port, enable):
    self.port = port
    self.ctl  = shutil.which("baram-ctl") if enable else None
    self.released = False

  def __enter__(self):
    if self.ctl:
      r = subprocess.run([self.ctl, "--port", self.port, "release"], capture_output=True)
      self.released = (r.returncode == 0)
      if self.released:
        print(f"{col('baram-term')} : 포트를 잠시 놓음")
    return self

  def __exit__(self, *exc):
    if self.released:
      subprocess.run([self.ctl, "--port", self.port, "resume"], capture_output=True)
      print(f"{col('baram-term')} : 포트 다시 엶")


def request_ok(ch, cmd, data=b"", timeout=3.0, what=""):
  r = ch.request(cmd, data, timeout=timeout)
  if r["err"]:
    sys.exit(f"{what or hex(cmd)} 실패 err={err_str(r['err'])}")
  return r


def change_baud(ch, tr, baud):
  """보드와 같이 보율을 바꾼다. 실패하면 115200 으로 돌아온다 (보드는 3 초 뒤 스스로 돌아온다)."""
  r = ch.request(BOOT_CMD_BAUD, struct.pack("<I", baud))
  if r["err"]:
    print(f"보율 {baud} 거부 err={err_str(r['err'])}, {BAUD_DEF} 로 진행")
    return BAUD_DEF
  tr.set_baud(baud)
  time.sleep(0.05)
  try:
    ch.request(BOOT_CMD_INFO, timeout=1.0)
    return baud
  except TimeoutError:
    print(f"보율 {baud} 에서 응답 없음. {BAUD_DEF} 로 돌아간다")
    tr.set_baud(BAUD_DEF)
    time.sleep(3.5)
    ch.request(BOOT_CMD_INFO, timeout=2.0)
    return BAUD_DEF


def download(ch, target, image, offset, args):
  if target == TARGET_FW:
    v = parse_version(ch.request(BOOT_CMD_VERSION)["data"])
    if v["img_type"]:
      print(f"{col('현재 FW')} : {v['name']} {v['version']}  [{v['img_str']}] {v['fw_size']} B  crc 0x{v['fw_crc']:04X}")
    else:
      print(f"{col('현재 FW')} : 없음")

  begin = struct.pack("<IB", len(image), target)
  if target == TARGET_DATA:
    begin += struct.pack("<I", offset)
  request_ok(ch, BOOT_CMD_FW_BEGIN, begin, what="BEGIN")

  t0 = time.time()
  request_ok(ch, BOOT_CMD_FW_ERASE, timeout=120.0, what="ERASE")
  print(f"  {col('지우기', 7)} {time.time()-t0:6.2f}s")

  t0 = time.time()
  for off in range(0, len(image), CHUNK):
    r = ch.request(BOOT_CMD_FW_WRITE, struct.pack("<I", off) + image[off:off+CHUNK], timeout=3.0)
    if r["err"]:
      sys.exit(f"\nWRITE off=0x{off:X} 실패 err={err_str(r['err'])}")
    if (off // CHUNK) % 8 == 0:
      print(f"\r  {col('쓰기', 7)} {off*100//len(image):3d}%", end="", flush=True)
  dt = time.time() - t0
  print(f"\r  {col('쓰기', 7)} {dt:6.2f}s  ({len(image)/dt/1024:.1f} KB/s)")

  t0 = time.time()
  r = request_ok(ch, BOOT_CMD_FW_END, timeout=60.0, what="END")
  size, crc = struct.unpack("<II", r["data"][:8])
  host_crc = crc16(image[:size])
  ok = (crc == host_crc)
  print(f"  {col('확인', 7)} {time.time()-t0:6.2f}s  {size} B  crc 0x{crc:04X} (호스트 0x{host_crc:04X}) {'OK' if ok else 'FAIL'}")
  if not ok:
    sys.exit("CRC 가 다르다")

  if target == TARGET_FW:
    r = ch.request(BOOT_CMD_FW_VERIFY, timeout=30.0)
    img = r["data"][0] if r["data"] else 0
    print(f"  {col('판정', 7)} {(IMG_TYPE[img] if img < len(IMG_TYPE) else '?'):>6}")


def main():
  ap = argparse.ArgumentParser(description="UART 로 앱 내려받기")
  ap.add_argument("binary", nargs="?", help="보낼 bin (기본 build/stm32h5-fw.bin)")
  ap.add_argument("--via", choices=["uart", "cdc", "hid"], default="uart", help="연결 방식 (기본 uart = ST-LINK VCP)")
  ap.add_argument("--port", help="시리얼 포트 (uart / cdc). 없거나 auto 면 자동 (uart: ST-LINK VCP, cdc: VID/PID)")
  ap.add_argument("--baud", type=int, default=2000000, help="UART 전송 보율 (기본 2 Mbps, 0 이면 115200 그대로)")
  ap.add_argument("--no-reset", action="store_true", help="끝난 뒤 앱을 실행하지 않는다")
  ap.add_argument("--no-baram", action="store_true", help="baram-term 포트 놓기/다시 열기를 하지 않는다")
  args = ap.parse_args()

  # 줄 단위로 내보내 진행 표시가 바로 보이게 한다
  sys.stdout.reconfigure(line_buffering=True)

  target = TARGET_FW
  args.offset = 0
  path = args.binary or DEF_FW
  if not path or not os.path.isfile(path):
    sys.exit(f"bin 이 없다: {path}")
  image = open(path, "rb").read()

  via  = args.via
  port = args.port if args.port and args.port.lower() != "auto" else None
  if via == "uart":
    port = port or find_port()
    if not port:
      sys.exit("시리얼 포트를 찾지 못했다. --port 로 줄 것")

  if via == "cdc":
    port = port or find_cdc_port()
    if not port:
      sys.exit("USB CDC 포트를 찾지 못했다. 보드의 USB 를 연결했는지 볼 것")

  # baram-term 이 포트를 쥐고 있으면 먼저 놓게 하고 연다 (UART / CDC). HID 는 해당 없다
  baram_port = port if via in ("uart", "cdc") else None
  link = Link(via, port)
  with BaramTerm(baram_port, (not args.no_baram) and baram_port is not None):
    try:
      link.open()
    except (OSError, IOError) as e:
      sys.exit(f"연결 실패 ({via}) : {e}")
    ch = CmdChannel(link.tr)
    try:
      info = parse_info(ch.request(BOOT_CMD_INFO, timeout=2.0)["data"])
      print(f"{col('연결')} : {link.name}  {info['name']} {info['version']}  [{info['mode_str']}]")
      if info["mode"] != DEV_MODE_BOOT:
        # 앱이 돌고 있다. 부트로더에 머물러 달라고 하고(FW_UPDATE → resetToBoot) 다시 붙는다
        print(f"{col('')}   앱이 실행 중 → 부트로더로 넘어가 다시 붙는다")
        try:
          ch.request(BOOT_CMD_FW_UPDATE, timeout=1.0)
        except TimeoutError:
          pass
        ch = CmdChannel(link.reopen())
        info = parse_info(ch.request(BOOT_CMD_INFO, timeout=3.0)["data"])
        print(f"{col('연결')} : {link.name}  {info['name']} {info['version']}  [{info['mode_str']}]")
        if info["mode"] != DEV_MODE_BOOT:
          sys.exit("부트로더로 넘어가지 않았다")
      if info["cmd_ver"] == 0:
        sys.exit("확장 INFO 가 없다 (다른 보드의 부트로더?)")

      print(f"{col('보낼 것')} : {os.path.basename(path)}  {len(image)/1024:.1f} KB -> {TARGET_STR[target]}"
            + (f" +0x{args.offset:X}" if target == TARGET_DATA else ""))

      baud = BAUD_DEF
      if via == "uart" and args.baud and args.baud != BAUD_DEF:
        baud = change_baud(ch, link.tr, args.baud)
      print(f"{col('보율')} : {baud if via == 'uart' else '- (USB)'}")

      t0 = time.time()
      download(ch, target, image, args.offset, args)
      print(f"{col('합계')} : {time.time()-t0:.2f}s")

      if baud != BAUD_DEF:
        try:
          ch.request(BOOT_CMD_BAUD, struct.pack("<I", BAUD_DEF), timeout=1.0)
        except TimeoutError:
          pass
        link.tr.set_baud(BAUD_DEF)

      if target == TARGET_FW and not args.no_reset:
        r = ch.request(BOOT_CMD_FW_JUMP, timeout=2.0)
        print(f"{col('실행')} : " + ("앱으로 점프" if r["err"] == 0 else f"실패 err={err_str(r['err'])}"))
    finally:
      link.close()


if __name__ == "__main__":
  main()
