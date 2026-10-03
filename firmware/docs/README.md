# STM32H563_CORE 펌웨어 문서

STM32H563RIT6 (LQFP64) / STM32H563_CORE Rev 1 보드 기준 펌웨어 개발 기록.

## 현재 상태 (2026-10-03)

| | |
|---|---|
| 보드 | STM32H563_CORE Rev 1 (회로도 `hardware/stm32h563_core_v1.pdf`, 2026-09-15) |
| 펌웨어 | **부트로더 `core/stm32h5-boot`** (0x0800_0000, 256 KB, UART 다운로드) + **앱 `core/stm32h5-fw`** (0x0804_0400) — LED, UART(VCP) + 로그 + CLI, RTC, 리셋 원인, 내장 플래시, QSPI, 폴트 기록. 보드에서 동작 확인 |
| 클럭 | HSE 25 MHz → PLL1 → **250 MHz** (VOS0) |
| 빌드 | `-Og` — 부트로더 70.3 KB / 254 KB, 앱 66.9 KB / 445 KB — arm-none-eabi-gcc 15.3.1 |
| 툴 | CubeCLT 1.22.0 에서 필요한 것만 `~/ST` 에 추출 (Programmer 2.23.0 / gdbserver 7.14.0). H563 SVD 추가 |
| 디버거 | ST-LINK V2-1, FW `V2J47M34` — 굽기·gdb 디버깅 확인. Device ID `0x484`. **VCP = USART1 (J1)**, 115200 |

### 바로 다시 시작하기

```bash
# 부트로더 (한 번만, SWD)
cd firmware/core/stm32h5-boot
cmake -S . -B build; cmake --build build -j20
python3 tools/flash.py --target boot

# 앱
cd ../stm32h5-fw
cmake -S . -B build; cmake --build build -j20
python3 tools/download.py              # UART (2 Mbps, 1 초). 앱이 돌고 있어도 된다
python3 tools/flash.py                 # 또는 SWD (TAG 를 붙여 0x0804_0000)
```

시리얼은 ST-LINK VCP 115200 이다. 프롬프트 `cli# ` 는 첫 입력 뒤에 나온다.
`download.py` 는 baram-term 이 포트를 열고 있으면 잠시 놓게 한다.

VSCode 앱 프로젝트에서는 `Debug FW`(빌드 → 쓰기 → 앱 `main` 에서 멈춤), `download-uart` / `download-uart (포트 선택)` 을 쓴다 → [26](26-bootloader.md).
macOS / Windows 모두 같은 구성이다 → [10](10-dev-environment.md)

### 다음 작업

1. LED 극성(active low) 눈 확인, 리셋 버튼 더블클릭 확인 (앱 / 부트로더 진입) ([20](20-led.md), [22](22-rtc-reset.md), [26](26-bootloader.md))
2. VSCode `Debug FW` 와 Windows 에서 툴 실행 확인
3. USB CDC (USB-C 로 CLI / 다운로드)
4. 슬롯 핑퐁 / 롤백 (w6300 방식, bank2)
5. (판단) QSPI 간접 읽기 개선 — XIP / HAL `-O2` / GPDMA ([24](24-qspi.md))

## 폴더 구성

코어보드(STM32H563_CORE) 위에 확장보드를 얹어 기능을 늘려 갈 예정이라, **보드별로 폴더를 나눈다.**
최종적으로는 `stm32h5-w6300` 처럼 부트로더 + 펌웨어(앱) 구조로 간다.

```
firmware/
├── docs/                 이 문서 (코어 / 확장보드 공통)
├── core/                 코어보드
│   ├── stm32h5-boot/     부트로더 + PC 툴(tools/). 확장보드도 같은 부트로더를 쓴다
│   └── stm32h5-fw/       코어보드 펌웨어 → 출력 stm32h5-fw.{elf,bin}
└── <확장보드>/            예정
    └── stm32h5-fw/       확장보드 펌웨어 → 출력 stm32h5-fw.{elf,bin}
```

| 규칙 | 이유 |
|---|---|
| 프로젝트 폴더 이름은 `stm32h5-boot` / `stm32h5-fw` | 참고 프로젝트(`stm32h5-w6300/firmware`)와 같은 이름. 저장소끼리 코드를 옮기기 쉽다 |
| 출력 파일 이름(`PRJ_NAME`), `_DEF_BOARD_NAME` 도 폴더 이름과 같게 (`stm32h5-fw` / `"STM32H5-FW"`) | 폴더와 산출물 이름이 어긋나지 않는다. 확장보드 펌웨어와 구분하는 방법은 확장보드를 붙일 때 정한다 |
| 부트로더는 `core/` 에 하나만 둔다 | 플래시 레이아웃은 코어보드(MCU)가 정한다. 확장보드는 앱만 다르다 |
| 각 프로젝트는 독립적으로 빌드된다 (`src/lib` 포함) | 참고 프로젝트 관례. 공통 코드를 뽑는 것은 확장보드가 생긴 뒤 판단한다 |

## 문서 번호 규칙

| 대역 | 성격 |
|---|---|
| `00~09` | 하드웨어 레퍼런스 — 데이터시트·RM·회로도에서 확정한 사실 |
| `10~19` | 개발 환경 / 프로젝트 구조 |
| `20~` | **기능별 구현 기록** — 기능 하나당 문서 하나 |

기능 문서는 "무엇을 왜 그렇게 했는지 + 막혔던 지점 + 검증 방법"을 남긴다.
코드만 봐서는 알 수 없는 것(보드 결선, 툴 버전 이슈, 참고 프로젝트와 달라진 점)이 대상이다.
**확인하지 못한 것은 확인하지 못했다고 적는다.**

## 목차

### 레퍼런스

| 문서 | 내용 |
|---|---|
| [01-board.md](01-board.md) | 보드 결선 — MCU, 클럭, LED, 디버그 커넥터, QSPI, USB, 전원 |

### 개발 환경

| 문서 | 내용 | 상태 |
|---|---|---|
| [10-dev-environment.md](10-dev-environment.md) | 툴체인, CubeCLT 경로 규칙 (macOS / Windows), VSCode 태스크·디버그, ST-LINK 연결 | ✅ (Windows 미확인) |
| [11-project-skeleton.md](11-project-skeleton.md) | `core/stm32h5-fw` 디렉터리/CMake 구조, 링커·스타트업, 참고 프로젝트와 다른 점 | ✅ |

### 구현 기록

| 문서 | 기능 | 상태 |
|---|---|---|
| [20-led.md](20-led.md) | LED (PC13) 500 ms 점멸 | ✅ (극성 눈 확인 남음) |
| [21-uart-cli.md](21-uart-cli.md) | UART(USART1, ST-LINK VCP) + 로그 + CLI, **TX/RX 스왑**, GPDMA 원형 수신 | ✅ |
| [22-rtc-reset.md](22-rtc-reset.md) | RTC(LSE) + 백업 레지스터 + 리셋 원인 / 더블클릭, `rtcIsTimeSet()` | ✅ (더블클릭 미확인) |
| [23-flash.md](23-flash.md) | 내장 플래시 (8 KB 섹터, 쿼드워드, 재기록 거부, 보호 영역), 레이아웃 | ✅ |
| [24-qspi.md](24-qspi.md) | QSPI W25Q256JV — 4 바이트 전용 명령, XIP 40 MB/s, **DCACHE1 무효화**, `flash.c` 주소 분기 | ✅ |
| [25-fault.md](25-fault.md) | 폴트 기록 — naked 진입부, CFSR/BFAR 원인, `.noinit` 에 남기고 리셋 | ✅ |
| [26-bootloader.md](26-bootloader.md) | **부트로더 + UART 다운로드** — 부트로더 256 KB, TAG 커밋, 2 Mbps 1 초, 앱 launch 디버깅, `-Og` | ✅ |

## 출처

| 약칭 | 문서 | 비고 |
|---|---|---|
| 회로도 | `hardware/stm32h563_core_v1.pdf` | Rev 1, 2026-09-15 |
| 참고 프로젝트 | `~/hdd/git/stm32h5-w6300/firmware/stm32h5-fw` | 같은 H563, 레이어 구조와 클럭 설정을 가져왔다 |
| 참고 문서 | `~/hdd/git/NUCLEO-N657X0/firmware/docs` | 문서 형식, CubeCLT 경로 규칙, 디버그 구성 |
