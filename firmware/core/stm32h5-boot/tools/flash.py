#!/usr/bin/env python3
"""
부트로더나 앱을 ST-LINK(SWD) 로 내장 플래시에 쓰고 리셋한다. (macOS / Linux / Windows)

  대상 (--target)
    boot : 부트로더 bin 을 0x08000000 에 쓴다
    fw   : 앱. TAG(1 KB)를 PC 에서 계산해 이미지 앞에 붙이고 0x08040000 (FLASH_ADDR_FIRM) 에 쓴다.
           UART 다운로드에서는 부트로더가 TAG 를 쓰지만, SWD 로 쓸 때는 이 스크립트가 같은 형식으로 만든다
           (TAG 가 없거나 CRC 가 다르면 부트로더가 앱을 실행하지 않는다)

  stm32n6-boot/tools/flash.py 를 바탕으로 했다. 외부 로더 / 서명은 필요 없다 (내장 플래시).

  CubeCLT 경로 : $CLT -> $STM32CLT_PATH (Windows 설치본) -> ~/ST/STM32CubeCLT(_*) -> /opt/ST/... -> C:/ST/...
  표준 라이브러리만 쓴다.

  사용 : python3 tools/flash.py [--target boot|fw] [--bin <bin>] [--no-reset]
         boot 기본: build/stm32h5-boot.bin, fw 기본: ../stm32h5-fw/build/stm32h5-fw.bin
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import crc16, col   # noqa: E402  (펌웨어 utilCalcCRC 와 같은 CRC-16)

IS_WIN   = os.name == "nt"
EXE      = ".exe" if IS_WIN else ""
PRJ_DIR  = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEF_BOOT = os.path.join(PRJ_DIR, "build", "stm32h5-boot.bin")
DEF_FW   = os.path.abspath(os.path.join(PRJ_DIR, "..", "stm32h5-fw", "build", "stm32h5-fw.bin"))
ADDR     = {"boot": "0x08000000", "fw": "0x08040000"}

TAG_SIZE      = 0x400               # FLASH_SIZE_TAG
TAG_MAGIC     = 0x54414720          # "TAG "
VER_MAGIC     = 0x56455220          # "VER "
VER_OFFSET    = 0x400               # 이미지 안 firm_ver_t 위치 (벡터 1 KB 뒤)
VER_SIZE_OFF  = VER_OFFSET + 4 + 32 + 32 + 4   # firm_ver_t.firm_size
ANSI     = re.compile(r"\x1b\[[0-9;]*m")


def find_clt():
  def tool(d):
    return os.path.join(d, "STM32CubeProgrammer", "bin", "STM32_Programmer_CLI" + EXE)

  def version_key(path):
    return [int(x) for x in re.findall(r"\d+", os.path.basename(path))]

  cands = []
  for env in ("CLT", "STM32CLT_PATH"):
    if os.environ.get(env):
      cands.append(os.environ[env])
  for base in [os.path.expanduser("~/ST"), "/opt/ST", "C:/ST"]:
    cands.append(os.path.join(base, "STM32CubeCLT"))
    cands += sorted(glob.glob(os.path.join(base, "STM32CubeCLT_*")), key=version_key, reverse=True)

  for c in cands:
    if os.path.isfile(tool(c)):
      return c
  return None


def run(args):
  # CubeProgrammer 출력에는 색 코드가 섞여 있다
  p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
  return p.returncode, ANSI.sub("", p.stdout.decode(errors="replace"))


def make_tagged(bin_path):
  """앱 bin 앞에 TAG 를 붙인 <이름>-tag.bin 을 만든다. 부트로더 cmdBootEndFw() 와 같은 형식이다.

     firm_tag_t = magic, fw_addr(=TAG_SIZE), fw_size, fw_crc, tag_crc
     fw_size 는 firm_ver_t.firm_size 를 우선한다 (부트로더와 같은 규칙).
  """
  img = open(bin_path, "rb").read()

  if len(img) < VER_SIZE_OFF + 4 or struct.unpack_from("<I", img, VER_OFFSET)[0] != VER_MAGIC:
    sys.exit(f"{bin_path} 에 firm_ver_t 가 없다 (앱 bin 이 맞나?)")

  fw_size = struct.unpack_from("<I", img, VER_SIZE_OFF)[0]
  if fw_size == 0 or fw_size > len(img):
    fw_size = len(img)

  fw_crc  = crc16(img[:fw_size])
  tag     = struct.pack("<IIII", TAG_MAGIC, TAG_SIZE, fw_size, fw_crc)
  tag    += struct.pack("<I", crc16(tag))
  tag    += b"\xFF" * (TAG_SIZE - len(tag))

  out = os.path.splitext(bin_path)[0] + "-tag.bin"
  open(out, "wb").write(tag + img[:fw_size])
  print(f"{col('TAG', 8)} : {fw_size} B  crc 0x{fw_crc:04X}  → {os.path.basename(out)}")
  return out


def main():
  ap = argparse.ArgumentParser(description="ST-LINK 로 부트로더 / 앱 쓰기")
  ap.add_argument("--target", choices=["boot", "fw"], default="boot")
  ap.add_argument("--bin", help="쓸 bin (boot: build/stm32h5-boot.bin, fw: ../stm32h5-fw/build/stm32h5-fw.bin)")
  ap.add_argument("--no-reset", action="store_true", help="쓴 뒤 리셋하지 않는다")
  args = ap.parse_args()

  path = args.bin or (DEF_FW if args.target == "fw" else DEF_BOOT)
  if not os.path.isfile(path):
    sys.exit(f"bin 이 없다: {path}")

  clt = find_clt()
  if not clt:
    sys.exit("CubeCLT 를 찾지 못했다. CLT=<경로> 또는 STM32CLT_PATH 로 지정할 것")
  prg = os.path.join(clt, "STM32CubeProgrammer", "bin", "STM32_Programmer_CLI" + EXE)
  print(f"{col('CubeCLT', 8)} : {clt}")

  if args.target == "fw":
    path = make_tagged(path)

  cmd = [prg, "-c", "port=SWD", "mode=UR", "-w", path, ADDR[args.target], "-v"]
  if not args.no_reset:
    cmd.append("-rst")

  print(f"{col('쓰기', 8)} : {os.path.basename(path)} → {ADDR[args.target]}")
  rc, out = run(cmd)
  ok = "Download verified successfully" in out
  for line in out.splitlines():
    if re.search(r"Error|verified|Erasing internal|ST-LINK SN|Device name", line):
      print(" " * 11 + line.strip())
  if rc != 0 or not ok:
    sys.exit("쓰기 실패")


if __name__ == "__main__":
  main()
