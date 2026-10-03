#!/usr/bin/env python3
"""빌드한 부트로더 / 앱 bin 을 웹페이지가 쓰는 자리(web/bin/)에 복사하고 manifest.json 을 만든다.

  python3 tools/release.py

  웹페이지(GitHub Pages)가 같은 사이트에서 bin 을 받아 바로 업데이트한다.
    boot/stm32h5-boot.bin   : ROM DFU (WebUSB) 로 0x08000000
    <보드>/stm32h5-fw.bin    : cmd 프로토콜 (WebHID / Web Serial) 로 부트로더가 FIRM 에 쓴다

  펌웨어는 보드마다 하나씩이다 (코어보드, 확장보드 …). 모두 이름이 stm32h5-fw.bin 이라 보드별 폴더로 나눈다.
  확장보드 펌웨어가 생기면 아래 FIRMWARES 에 한 줄 더한다. 빌드가 없는 항목은 건너뛴다.

  버전 / 이름은 bin 안의 firm_ver_t (벡터 1 KB 뒤) 에서 읽는다. 빌드는 하지 않는다.
  부트로더와 앱 프로젝트에 같은 파일을 둔다. 표준 라이브러리만 쓴다.
"""
import datetime
import hashlib
import json
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import crc16   # noqa: E402  (펌웨어 utilCalcCRC 와 같은 CRC-16)

CORE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REPO_DIR = os.path.abspath(os.path.join(CORE_DIR, "..", ".."))
OUT_DIR  = os.path.join(REPO_DIR, "web", "bin")

VER_MAGIC  = 0x56455220          # "VER "
VER_OFFSET = 0x400

FW_DIR    = os.path.abspath(os.path.join(CORE_DIR, ".."))       # firmware/
BOOT_ADDR = 0x08000000
APP_ADDR  = 0x08040400

BOOT = os.path.join(CORE_DIR, "stm32h5-boot", "build", "stm32h5-boot.bin")

FIRMWARES = [
  # id (web/bin 아래 폴더), 화면에 보일 이름, bin
  ("core", "코어보드", os.path.join(FW_DIR, "core", "stm32h5-fw", "build", "stm32h5-fw.bin")),
  # ("hg-swd-prog", "확장보드 hg-swd-prog", os.path.join(FW_DIR, "hg-swd-prog", "stm32h5-fw", "build", "stm32h5-fw.bin")),
]


def read_ver(img):
  """firm_ver_t : magic, version_str[32], name_str[32], firm_addr, firm_size"""
  if len(img) < VER_OFFSET + 76 or struct.unpack_from("<I", img, VER_OFFSET)[0] != VER_MAGIC:
    return None
  z = lambda b: b.split(b"\0")[0].decode("ascii", "replace")
  version = z(img[VER_OFFSET + 4:VER_OFFSET + 36])
  name    = z(img[VER_OFFSET + 36:VER_OFFSET + 68])
  addr, size = struct.unpack_from("<II", img, VER_OFFSET + 68)
  return dict(name=name, version=version, firm_addr=addr, firm_size=size)


def copy_image(path, rel, addr, method):
  """bin 을 web/bin/<rel> 로 복사하고 manifest 항목을 돌려준다."""
  img = open(path, "rb").read()
  ver = read_ver(img)
  if ver is None:
    sys.exit(f"{path} 에 firm_ver_t 가 없다")
  if ver["firm_addr"] != addr:
    sys.exit(f"{path} 의 firm_addr 0x{ver['firm_addr']:08X} 가 0x{addr:08X} 가 아니다 (레이아웃이 바뀌었나?)")

  dst = os.path.join(OUT_DIR, rel)
  os.makedirs(os.path.dirname(dst), exist_ok=True)
  shutil.copyfile(path, dst)
  print(f"  {rel:28s} {ver['name']} {ver['version']}  {len(img)} B")
  return {
    "file":    rel,
    "name":    ver["name"],
    "version": ver["version"],
    "addr":    f"0x{addr:08X}",
    "size":    len(img),
    "crc16":   f"0x{crc16(img):04X}",
    "sha256":  hashlib.sha256(img).hexdigest(),
    "method":  method,
  }


def main():
  if os.path.isdir(OUT_DIR):
    shutil.rmtree(OUT_DIR)          # 목록에서 빠진 보드의 옛 bin 이 남지 않게
  os.makedirs(OUT_DIR)

  manifest = {"generated": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), "boot": None, "firmwares": []}

  if not os.path.isfile(BOOT):
    sys.exit(f"부트로더 bin 이 없다: {BOOT} (먼저 빌드할 것)")
  manifest["boot"] = copy_image(BOOT, "boot/stm32h5-boot.bin", BOOT_ADDR, "dfu")

  for fid, title, path in FIRMWARES:
    if not os.path.isfile(path):
      print(f"  ({fid}: 빌드 없음, 건너뜀 — {path})")
      continue
    item = copy_image(path, f"{fid}/stm32h5-fw.bin", APP_ADDR, "cmd")
    manifest["firmwares"].append({"id": fid, "title": title, **item})

  if not manifest["firmwares"]:
    sys.exit("펌웨어 bin 이 하나도 없다 (먼저 빌드할 것)")

  with open(os.path.join(OUT_DIR, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2, ensure_ascii=False)
    f.write("\n")
  print("manifest : web/bin/manifest.json")


if __name__ == "__main__":
  main()
