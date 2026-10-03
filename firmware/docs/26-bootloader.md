# 26. 부트로더 + UART 다운로드

> 부트로더(`core/stm32h5-boot`)와 앱(`core/stm32h5-fw`)을 나눴다. 디버거 없이 CLI 와 같은 UART(ST-LINK VCP) 로 앱을 내려받는다.
> 프로토콜·이미지 형식·호스트 툴은 N6(`stm32n6-boot`) 것, 플래시 드라이버·점프 방식은 w6300(`stm32h5-boot`) 것이다.
> 관련: [21](21-uart-cli.md) (cli RX 필터), [22](22-rtc-reset.md) (부트 요청 / 더블클릭), [23](23-flash.md) (내장 플래시)
>
> **상태: 보드에서 확인 (2026-10-03).** 단순형 레이아웃. 슬롯 핑퐁 / 롤백은 다음 단계.

---

## 1. 쓰는 법

```bash
# 부트로더 (한 번만, SWD)
cd firmware/core/stm32h5-boot
cmake -S . -B build; cmake --build build -j20
python3 tools/flash.py --target boot

# 앱
cd ../stm32h5-fw
cmake -S . -B build; cmake --build build -j20
python3 tools/download.py                # UART. 앱이 돌고 있어도 된다
python3 tools/flash.py                   # 또는 SWD (TAG 를 PC 에서 붙여 0x0804_0000)
```

```
baram-term : 포트를 잠시 놓음
연결       : /dev/cu.usbmodem1412302  STM32H5-FW V261003R1  [APP]
             앱이 실행 중 → 부트로더로 넘어가 다시 붙는다
연결       : /dev/cu.usbmodem1412302  STM32H5-BOOT V261003R1  [BOOT]
보낼 것    : stm32h5-fw.bin  67.4 KB -> FW
보율       : 2000000
현재 FW    : STM32H5-FW V261003R1  [TAG] 68976 B  crc 0x0B5F
  지우기    0.02s
  쓰기      0.65s  (103.3 KB/s)
  확인      0.02s  68976 B  crc 0x0B5F (호스트 0x0B5F) OK
  판정       TAG
합계       : 0.71s
실행       : 앱으로 점프
baram-term : 포트 다시 엶
```

툴의 라벨은 `cmdproto.col()` 로 **화면 폭** 기준으로 채운다. 한글은 터미널에서 2 칸이라 글자 수로 채우면 열이 어긋난다.

| VSCode | 프로젝트 | 하는 일 |
|---|---|---|
| 태스크 `flash-stlink` | boot | `tools/flash.py --target boot` |
| 태스크 `flash-stlink` | fw | `tools/flash.py --target fw` (TAG 붙여 0x0804_0000) |
| 태스크 `download-uart` | fw | `tools/download.py` (ST-LINK VCP 자동) |
| 태스크 `download-uart (포트 선택)` | fw | `tools/download.py --port <고른 포트>`. Firmware Task Manager 확장(`firmware-task.pickSerialPort`)이 목록을 띄운다 |
| 런치 `Debug Boot` / `Attach Boot` | boot | 부트로더 디버깅 |
| 런치 **`Debug FW`** | fw | 빌드 → TAG 붙여 쓰기(`build-flash`) → 리셋 → 부트로더가 앱으로 점프 → **앱 `main` 에서 멈춤** |
| 런치 `Flash + Attach FW` | fw | `flash-stlink` 로 쓰고 리셋 → 부트로더가 실행한 앱에 attach (멈추지 않고 붙기) |
| 런치 `Attach FW` | fw | 돌고 있는 앱에 붙기만 |

### 앱을 launch 로 디버깅하기

H5 는 내장 플래시에서 바로 실행하므로 (N6 는 FSBL 이 SRAM 으로 복사) 앱도 launch 로 `main` 부터 디버깅할 수 있다. 한 가지만 막으면 된다.

**디버거가 ELF 를 쓰면 TAG 가 지워진다.** TAG(0x0804_0000) 가 앱 첫 8 KB 섹터 안에 있어, 디버거가 앱 벡터(0x0804_0400) 를 쓰려고
그 섹터를 지우면 TAG 도 같이 사라진다. 리셋 뒤 부트로더는 앱이 없다고 보고 머문다.

그래서 `Debug FW` 는

1. `preLaunchTask: build-flash` — 빌드 후 `flash.py --target fw` 로 TAG 를 붙여 쓴다
2. `loadFiles: []` — 디버거는 아무것도 쓰지 않는다. 심볼만 `executable` 에서 읽는다
3. 리셋 후 실행 → 부트로더가 TAG 를 확인하고 점프 → `runToEntryPoint: main` 이 앱 `main` 에서 멈춘다

같은 순서를 gdb 로 흉내 내 확인했다 (cortex-debug 자체로는 아직 눌러 보지 않았다. 아래 주소는 부트로더 128 KB 때다).

```
after reset PC=0x080034fc                        <- 부트로더 안
Breakpoint 1, main () at .../stm32h5-fw/src/main.c:8      <- 0x08022374, 앱 main
VTOR=0x08020400
Breakpoint 2, apMain () at .../stm32h5-fw/src/ap/ap.c:15
```

부트로더 코드는 앱 ELF 에 심볼이 없어 단계 실행할 수 없다. 부트로더는 `stm32h5-boot` 의 `Debug Boot` 로 한다.
툴은 표준 라이브러리 + pyserial 이고 Windows 에서는 `python` 으로 부른다.

---

## 2. 플래시 레이아웃 (단순형)

| 주소 | 크기 | 영역 |
|---|---|---|
| `0x0800_0000` | **256 KB** | 부트로더 (벡터, +0x400 에 `firm_ver_t`) |
| `0x0804_0000` | 1 KB | **TAG** (`firm_tag_t`) — 부트로더가 마지막에 쓴다 = 커밋 |
| `0x0804_0400` | ~447 KB | 앱 (벡터, +0x400 에 `firm_ver_t`) |
| `0x080B_0000` | 320 KB | 뱅크1 남는 곳 |
| `0x0810_0000` | 1 MB | 뱅크2 — 비어 있음 (슬롯 단계에서 쓴다) |

- 앱 링커 : `VECTOR 0x0804_0400 / VER 0x0804_0800 / FLASH 0x0804_0C00`. TAG 는 앱 bin 에 들어가지 않는다
- 앱 `firm_ver_t.firm_size` = 링커 심볼 `_fw_flash_size` (`LOADADDR(.data) + SIZEOF(.data) - _fw_flash_begin`). **bin 크기와 같다**
- 부트로더는 자기 자리(256 KB)만 보호하고 FIRM 을 쓴다. 앱은 뱅크1 전체를 보호한다

### 부트로더를 128 KB 에서 256 KB 로 키운 이유

처음에는 w6300 과 같은 128 KB 였다. USB(CDC / MSC / HID + UF2) 를 부트로더에 넣으려면 모자란다.

| 빌드 옵션 | 부트로더 (USB 없음) | USB + UF2 추가 시 (w6300 크기에서 짐작) |
|---|---|---|
| `-O0` | 96.9 KB | 약 140 KB — 128 KB 를 넘는다 |
| `-Og` | 70.3 KB | 약 105 KB |
| `-O2` | 69.2 KB | |
| `-Os` | 63.2 KB | 약 100 KB (w6300 부트로더 `-Os` 실측 86.5 KB) |

뱅크1 은 1 MB 인데 128 KB + 앱 448 KB 로 448 KB 가 비어 있었다. 부트로더를 256 KB 로 키워도 **앱 크기는 그대로**이고
슬롯(뱅크2)에도 영향이 없다. 그래서 키웠다. 주소가 w6300 과 달라졌지만 툴은 주소를 INFO 응답과 `hw_def.h` 에서 가져오므로
고친 곳은 두 프로젝트의 `hw_def.h`, 링커 스크립트, `flash.py` 의 주소뿐이다.

### 빌드 옵션 `-Og`

부트로더·앱 모두 `-Og` 로 바꿨다 (HAL 도 같다. 전에는 전부 `-O0`). 디버깅용 최적화라 브레이크포인트·백트레이스·인자·지역변수가 그대로 보이고
크기는 `-O2` 수준으로 준다.

| | `-O0` | `-Og` |
|---|---|---|
| 부트로더 | 96.9 KB | **70.3 KB** / 254 KB (27%) |
| 앱 | 92.0 KB | **66.9 KB** |
| QSPI 간접 읽기 | 2.9 MB/s | 4.0 MB/s |

```
Breakpoint 2, ledToggle (ch=ch@entry=0 '\000') at .../src/hw/driver/led.c:62
#1  0x08041d0c in apMain () at .../src/ap/ap.c:25
(gdb) info locals
pre_time = 548
```

## 3. 부팅 흐름

| 상황 | 부트로더 판정 | 실측 로그 |
|---|---|---|
| 리셋 / 전원, 앱 유효 | TAG + CRC 맞음 | `[  ] jump : 0x080427AD` → `[ Firmware Begin... ]` `Addr 0x8040400` |
| 앱이 없음 | `bootVerifyFirm() != TAG` | `[  ] boot : stay in bootloader (no valid app)` |
| 앱에서 `reset boot` / cmd `FW_UPDATE` | 백업 레지스터 `MODE_BIT_BOOT` | `stay in bootloader (boot request)` |
| 리셋 버튼 두 번 (300 ms 안) | `reset_count ≥ 2` | (아직 눌러 보지 않았다) |
| 부트로더 CLI `boot jump` / cmd `FW_JUMP` | TAG 확인 | 앱으로 |

부트로더 LED 는 100 ms, 앱은 500 ms 로 깜빡인다.

### 점프 (`bootJumpFirm`) — w6300 방식

부트로더는 **VTOR 도 MSP 도 건드리지 않는다.** 앱 벡터 `+4`(Reset_Handler) 를 읽어 직접 부른다.

- MSP : 앱 `Reset_Handler` 의 `ldr sp, =_estack`
- VTOR : 앱 `SystemInit()` 의 `SCB->VTOR = &_fw_flash_begin` (처음부터 이렇게 해 두었다, [11](11-project-skeleton.md))

점프 전 정리 : `uartClose()` (원형 수신 DMA 정지, N6 `uart.c` 가 이것을 해 준다) → `bspDeInit()` (NVIC 끄기 + **대기 지우기(ICPR)**, SysTick 끄기, MPU 끄기).
RCC 는 되돌리지 않는다. 앱 `SystemInit()` 이 리셋 상태로 되돌린다.

앱에 attach 해서 확인 (128 KB 때) : `VTOR = 0x08020400`, `SystemCoreClock = 250000000`, 백트레이스가 앱 함수.

### 리셋 원인

부트로더는 `HW_RESET_BOOT 1` — RCC 리셋 플래그를 읽고 지운 뒤 백업 레지스터에 남긴다.
앱은 `HW_RESET_BOOT 0` 으로 바꿨다 — 백업 레지스터의 값을 읽는다. 앱 로그의 `RESET_BIT_PIN / SOFT` 는 부트로더가 넘긴 것이다.

---

## 4. 프로토콜과 코드 구조

N6 와 같다 (원본 설명은 `NUCLEO-N657X0/firmware/docs/28-uart-download.md`). 요점만:

- 패킷 `02 FD type cmd err len data checksum`, 데이터 최대 1024 B
- CLI 와 같은 UART. **cli 의 RX 필터**가 `02 FD` 로 시작하는 바이트만 cmd 큐로 가져가고 나머지는 cli 로 간다
- 115200 으로 붙어 `BAUD` 로 올리고, 끝나면 되돌린다. 새 보율로 3 초 동안 주고받는 것이 없으면 보드가 스스로 115200 으로 돌아온다

| 파일 | 출처 | |
|---|---|---|
| `ap/modules/module.c/h` | N6 | `MODULE_DEF` 자기 등록 (`.module` 섹션). 링커 스크립트에 이미 있었다 |
| `ap/modules/common/cli/cli.c` | N6 | cli 모듈 |
| `ap/modules/cmd/cmd_task.c`, `driver/drv_uart.c` | N6 | cmd 채널 (RX 필터, 보율 복귀) |
| `ap/modules/cmd/process/cmd_boot.c` | N6 → **H5 로 고침** | 아래 |
| `ap/modules/boot/boot.c` | N6 판정 + w6300 점프 | 앱 이미지 판정 (TAG → VER), `boot info / jump` CLI |
| `hw/driver/cmd.c`, `common/def.h` | N6 | 패킷 파서, `firm_ver_t.firm_size`, `HW_DEV_MODE_*` |
| `tools/download.py`, `cmdproto.py`, `flash.py` | N6 → H5 로 고침 | 부트로더와 앱 프로젝트에 **같은 툴을 하나씩** 둔다 (프로젝트마다 독립, w6300 관례). 기본값만 다르다 — 앱 쪽은 `flash.py` 기본 대상이 `fw`, bin 이 자기 `build/` |

앱(`stm32h5-fw`) 도 module / cli / cmd 를 쓴다. 앱의 `cmd_boot.c` 는 N6 `stm32n6-fw` 것 — INFO(mode = APP), FW_UPDATE / FW_JUMP → `resetToBoot()`, BAUD, RESET 만.

### `cmd_boot.c` 를 H5 내장 플래시에 맞춘 것

| | N6 (외부 NOR) | H5 (내장 플래시) |
|---|---|---|
| 대상 | FW / BOOT(FSBL1·2) / DATA | **FW 만.** 부트로더는 자기 자리에서 돌아 자기를 고쳐 쓸 수 없다 |
| TAG | 독립 4 KB 섹터 | 앱 첫 **8 KB 섹터 안** 1 KB |
| BEGIN | TAG 섹터 지우기 | TAG 가 든 첫 섹터 지우기 (앱 벡터도 함께 — 어차피 다시 쓴다) |
| ERASE | 64 KB 블록 단위 | **8 KB 섹터** 단위 |
| WRITE | 아무 오프셋 | **16 B(쿼드워드) 정렬**만 받는다 |
| END | TAG 지우고 다시 쓰기 | 지우지 않고 **ERASE 가 비워 둔 TAG 자리에 쓰기만** (지우면 앱 벡터까지 지워진다) |

### 호스트 툴

| | N6 | H5 |
|---|---|---|
| `download.py` 조각 | 1016 B | **1008 B** (16 의 배수여야 쓰기 정렬을 지킨다) |
| 기본 보율 | 4 Mbps | **2 Mbps** (ST-LINK V2-1 VCP 한계, 아래) |
| `flash.py` | 외부 로더 + 서명 | 내장 플래시. `fw` 는 TAG(1 KB) 를 앞에 붙여 0x0804_0000 에. CubeCLT 경로에 `STM32CLT_PATH` 추가 |

---

## 5. 측정

92 KB 앱 (94,096 B, `-O0` 때). `-Og` 앱 67 KB 는 2 Mbps 에서 합계 0.68 s.

| 보율 | 쓰기 | 합계 |
|---|---|---|
| 115200 | 10.6 KB/s | 8.8 s |
| 921600 | 65.5 KB/s | 1.5 s |
| **2 M (기본)** | 98.6 KB/s | **1.0 s** |
| 2.5 M / 3 M / 4 M | 응답 없음 → 115200 으로 돌아가 끝까지 씀 | 8.8 s |

ST-LINK V2-1 의 VCP 가 2 Mbps 까지 받는다. USART1 은 250 MHz 클럭이라 더 낼 수 있다.
보율을 못 올린 경우의 복귀(호스트 115200 + 보드 3 초 복귀)가 실제로 동작하는 것을 확인했다.

지우기는 0.02 s (FIRM 섹터 12 개). N6 외부 NOR 의 1 s 와 비교해 무시할 만하다.

---

## 6. 검증

> 아래 주소(0x0802_xxxx)는 부트로더 128 KB 때 측정이다. 256 KB 로 옮긴 뒤 다운로드 → 점프(`0x080427AD`, `Addr 0x8040400`), 리셋 → 앱, `-Og` 디버깅을 다시 확인했다.

| 시험 | 결과 |
|---|---|
| 부트로더만 굽기 (앱 없음) | `stay in bootloader (no valid app)`, 모듈 boot / cmd / cli OK |
| `download.py` (부트로더 상태) | 쓰기 → CRC 일치 → TAG → `FW_JUMP` → 앱 |
| `download.py` (앱이 도는 상태, `[APP]`) | `FW_UPDATE` → 부트로더 → 쓰기 → 앱 |
| 앱 `reset reset` | 부트로더 → `jump : 0x08022D11` → 앱 |
| 앱 `reset boot` | `stay in bootloader (boot request)` |
| 부트로더 `boot info` / `boot jump` | `image : TAG`, `tag : 94096 bytes, crc 0x26A6` / 앱으로 |
| `flash.py --target fw` | TAG crc `0x26A6` (부트로더가 만든 것과 같다) → 리셋 → 앱 |
| `flash.py --target boot` | 부트로더만 다시 굽기 → 앱 그대로 실행 |
| gdb attach (돌고 있는 앱) | 멈춤 → 백트레이스 앱 함수, `VTOR 0x08020400` → detach 후 계속 동작 |

### 남은 것

- [ ] 리셋 버튼 더블클릭으로 부트로더 진입
- [ ] VSCode 에서 `Debug FW` / `Flash + Attach FW` 직접 실행
- [ ] Windows 에서 `download.py` / `flash.py`
- [x] 부트로더 크기 — 256 KB 로 키우고 `-Og` 로 70.3 KB (27%)

---

## 7. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `cmd_boot.c` 컴파일 오류 (정의 중복, 정의 안 된 변수) | N6 소스를 스크립트로 고칠 때 함수 **정의**를 찾는다는 게 같은 문자열로 시작하는 맨 위 **선언**에 걸렸다 | 원본에서 다시 시작해 줄 머리의 정의(`^sig\n{`)만 찾아 고쳤다 |
| `boot.c` 의 `MODULE_DEF` 오류 | `module.h` 를 include 하지 않음 | include 추가 |
| (설계) 4 Mbps 다운로드 실패 | ST-LINK V2-1 VCP 한계 | 기본 2 Mbps. 실패 시 115200 복귀는 동작한다 |
| (설계) 앱 `flash-stlink` 가 0x0800_0000 에 쓰고 있었다 | 부트로더 이전 설정 그대로 | `flash.py --target fw` 로 바꿨다 (TAG + 0x0802_0000) |
| (설계) USB 를 넣기에 128 KB 부트로더가 모자란다 | `-O0` 96.9 KB + USB | 256 KB 로 키우고 `-Og` (2 절) |
| (설계) 앱을 launch 로 디버깅하면 TAG 가 지워진다 | 디버거가 TAG 가 든 첫 섹터를 지운다 | `loadFiles: []` + `preLaunchTask: build-flash` |
