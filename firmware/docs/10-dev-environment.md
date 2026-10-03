# 10. 개발 환경

> 2026-10-03 실측. 호스트: macOS 27.0 (Darwin 27.0.0), Apple Silicon.
> **macOS 와 Windows 양쪽에서 같은 태스크·런치 구성으로 빌드하고 디버깅한다.**
> CubeCLT 경로 규칙은 `NUCLEO-N657X0/firmware/docs/10-dev-environment.md` 를 그대로 따른다.

---

## 1. 결론 요약

| 항목 | macOS | Windows |
|---|---|---|
| 컴파일러 | Homebrew arm-none-eabi-gcc 15.3.1 ✅ | `ARM_TOOLCHAIN_DIR` 또는 PATH |
| 빌드 | Homebrew cmake 4.4.3 + `/usr/bin/make` 3.81 ✅ | cmake + MinGW make (`-G 'MinGW Makefiles'`) |
| CubeCLT | `~/ST/STM32CubeCLT` (필요한 것만 추출, 버전 없는 링크) ✅ | 설치 프로그램 → `STM32CLT_PATH` 환경변수 |
| SVD | `STM32H563.svd` 추가 추출 ✅ | CubeCLT 에 포함 |
| 보드 연결 | ✅ ST-LINK V2 (`V2J47M34`) — 굽기, gdbserver 디버깅 | 미확인 |

---

## 2. 컴파일러 / 빌드 도구

```
arm-none-eabi-gcc (Arm GNU Toolchain 15.3.Rel1 (Build arm-15.149)) 15.3.1   /opt/homebrew/bin
cmake version 4.4.3                                                         /opt/homebrew/bin
make  3.81                                                                  /usr/bin
```

`tools/arm-none-eabi-gcc.cmake` 는 N6 프로젝트 것을 가져왔다. 참고 프로젝트(`stm32h5-w6300`) 것과 다른 점은
탐색 힌트 두 가지다.

| 힌트 | 이유 |
|---|---|
| macOS 에서 `/opt/homebrew/bin` 을 컴파일러 후보에 추가 | CubeCLT pkg 를 설치하면 `/etc/paths` 맨 앞에 번들 gcc 14.3.1 이 끼어든다. 그래도 Homebrew 것을 잡게 한다 |
| Windows 가 아니면 `/usr/bin` 을 make 후보 맨 앞에 | CubeCLT 안의 make 가 캐시에 박히면 CubeCLT 를 갈아끼울 때 빌드가 깨진다 |

Windows 에서는 `C:/MinGW-32/bin` 과 `MAKE_DIR` 에서 make 를, `ARM_TOOLCHAIN_DIR` 에서 컴파일러를 찾는다.

### 병렬 빌드는 `-j20` 고정

N6 문서 3절과 같은 이유다. OS 마다 코어 수를 세는 방법이 달라 상수로 두고,
숫자 없는 `-j` 는 make 에서 "제한 없음" 이라 쓰지 않는다.

### 구성과 빌드는 `;` 로 잇는다

`&&` 는 Windows PowerShell 5.1 에서 안 된다. 구성이 실패해도 빌드가 이어서 돌지만 어차피 실패하므로 문제없다.

### Windows 는 생성기를 지정한다

Windows 에서 cmake 기본 생성기는 Visual Studio 다. 툴체인 파일이 make 를 찾아도 생성기가 맞지 않으면
소용없으므로 `tasks.json` 의 `windows` 명령에 `-G 'MinGW Makefiles'` 를 붙였다.

---

## 3. CubeCLT

### macOS — 필요한 것만 `~/ST` 에 추출

N6 작업 때 이미 추출해 둔 `~/ST/STM32CubeCLT_1.22.0` 과 링크 `~/ST/STM32CubeCLT` 를 같이 쓴다.
SVD 는 N657 만 뽑아 두었으므로 **H563 을 같은 pkg 에서 추가로 뽑았다** (15 MB).

```bash
PKG=~/Downloads/stm32cubeclt_1.22.0_29188_20260626_1359-Mac-aarch64
TMP=$(mktemp -d)
pkgutil --expand "$PKG/stm32cubeclt_1.22.0_29188_20260626_1359-Mac-aarch64.pkg" "$TMP/pkg"
cd ~/ST/STM32CubeCLT_1.22.0
gunzip -c "$TMP/pkg/tmp.pkg/Payload" | cpio -idm './STMicroelectronics_CMSIS_SVD/STM32H563.svd'
rm -rf "$TMP"
```

```
~/ST/STM32CubeCLT/STMicroelectronics_CMSIS_SVD/
  STM32H563.svd    15,527,273 B
  STM32N657.svd    23,156,237 B
```

### Windows — `STM32CLT_PATH`

Windows 는 설치 프로그램으로 `C:\ST\STM32CubeCLT_<버전>` 에 설치한다.
경로에 버전이 들어가므로 JSON 에 박지 않고 **`STM32CLT_PATH` 환경변수**로 참조한다.

⚠️ CubeCLT 설치 프로그램이 `STM32CLT_PATH` 를 등록한다고 알고 있지만 **이 프로젝트에서 확인하지는 않았다.**
없으면 직접 등록한다.

```powershell
setx STM32CLT_PATH "C:\ST\STM32CubeCLT_1.22.0"    # VSCode 를 다시 띄워야 반영된다
```

### 참조하는 곳

| 파일 | macOS | Windows |
|---|---|---|
| `.vscode/tasks.json` (`flash-stlink`, `device-reset`) | `${userHome}/ST/STM32CubeCLT/...` | `${env:STM32CLT_PATH}/...` |
| `.vscode/launch.json` (`osx` / `windows` 섹션) | 〃 | 〃 |

버전을 올릴 때는 macOS 는 링크만, Windows 는 환경변수만 바꾸면 되고 고칠 파일은 없다.

---

## 4. VSCode 구성

### 태스크

| 태스크 | 하는 일 |
|---|---|
| `build-build` | 구성 + 빌드 (기본 빌드 태스크) |
| `build-clean` | clean |
| `flash-stlink` | `STM32_Programmer_CLI` 로 `.bin` 을 0x08000000 에 쓰고 리셋. 빌드는 하지 않는다 |
| `device-reset` | 리셋만 |

참고 프로젝트에 있던 pyocd 태스크는 뺐다. 이 호스트에 pyocd 가 없고, ST-LINK + CubeCLT 로 통일한다.

### 런치 (cortex-debug, `servertype: stlink`)

| 구성 | 동작 |
|---|---|
| `Debug FW` | `build-build` → gdbserver 가 리셋 후 ELF 를 쓰고 → `main` 에서 멈춤 |
| `Attach FW` | 돌고 있는 보드에 리셋 없이 붙는다. 쓰기 안 함. **보드에서 돌고 있는 것과 같은 ELF 여야 한다** |

H5 는 내장 플래시에서 바로 부팅하므로 N6 처럼 적재 방식(SRAM 적재, `load.sh`)을 따로 둘 필요가 없다.
cortex-debug 의 launch 가 플래시에 쓰고 리셋하는 것으로 충분하다.

`c_cpp_properties.json` 은 `build/compile_commands.json` 을 쓰므로 OS 와 무관하다.

---

## 5. 하드웨어 연결

2026-10-03 확인.

```
$ STM32_Programmer_CLI -l st-link
ST-LINK SN  : 0673FF525784864967204317
ST-LINK FW  : V2J47M34
Access Port Number  : 2

$ STM32_Programmer_CLI -c port=SWD mode=UR
Voltage     : 3.22V
Device ID   : 0x484
Revision ID : Rev X
Device name : STM32H56x/573
Device CPU  : Cortex-M33
```

### gdbserver 7.14.0 은 ST-LINK V2 펌웨어 `V2J47M34` 를 그대로 받아들였다

N6 에서는 gdbserver 가 ST-LINK V3 출고 펌웨어를 거부해 업그레이드가 필요했다.
이 ST-LINK V2 는 업그레이드 없이 붙는다.

```bash
CLT=~/ST/STM32CubeCLT
$CLT/STLink-gdb-server/bin/ST-LINK_gdbserver -p 61234 -l 1 -d -s -m 1 -cp $CLT/STM32CubeProgrammer/bin
arm-none-eabi-gdb build/stm32h5-fw.elf -ex "target extended-remote :61234" -ex "monitor reset" -ex "load"
```

```
STM32 Successfully completed reset operation (System reset)
Start address 0x08000cec, load size 8724
Transfer rate: 10 KB/sec
```

`Debug FW` 런치 구성 자체(VSCode cortex-debug)는 아직 VSCode 에서 눌러 보지 않았다. 같은 gdbserver 와 같은 동작이다.

---

## 6. 체크리스트

- [x] arm-none-eabi-gcc 15.3.1 / cmake 4.4.3 / make 3.81
- [x] CubeCLT 1.22.0 (`~/ST/STM32CubeCLT`) — Programmer 2.23.0
- [x] `STM32H563.svd` 추출
- [x] macOS 빌드
- [x] ST-LINK 연결, Device ID 확인 (`0x484`)
- [x] 굽기 (`flash-stlink` 명령), gdbserver + gdb 로 load / 브레이크
- [ ] VSCode 에서 `Debug FW`, `Attach FW` 직접 실행
- [ ] Windows 빌드 / 디버그
