# stm32h563-core

STM32H563 (Cortex-M33, 250 MHz) 코어보드 **STM32H563_CORE** 의 부트로더와 펌웨어를 처음부터 만든 프로젝트입니다.
STM32CubeIDE 없이 CMake + arm-none-eabi-gcc + VSCode 로 빌드하고, macOS / Windows 에서 같은 방식으로 씁니다.

코어보드 위에 확장보드를 얹어 기능을 늘려 가는 구성입니다. 부트로더는 코어보드에 하나만 두고, 코어보드용 · 확장보드용 펌웨어가 그 위에서 돕니다.
만들면서 알게 된 것(보드 결선, 드라이버 함정, 툴 이슈)은 기능별 문서로 남깁니다.

**웹페이지 : <https://chcbaram.github.io/stm32h563-core/>** — 브라우저(Chrome / Edge)에서 보드를 연결해 펌웨어(WebHID / Web Serial)와 부트로더(WebUSB, ROM DFU)를 업데이트합니다. 저장소에 넣어 둔 이미지(`web/bin`)로 바로 업데이트할 수 있습니다.

## 주요 기능

| 기능 | 내용 | 문서 |
|---|---|---|
| UART / CLI | 디버그 커넥터의 USART1 을 ST-LINK VCP 로. DMA 원형 수신, 로그 링 버퍼, CLI | [21](firmware/docs/21-uart-cli.md) |
| RTC / 리셋 | LSE RTC, 백업 레지스터로 부트 모드 전달, 리셋 원인, 리셋 두 번 눌러 부트로더 진입 | [22](firmware/docs/22-rtc-reset.md) |
| 내장 플래시 | 8 KB 섹터, 쿼드워드 쓰기, 재기록 거부(ECC), 보호 영역 | [23](firmware/docs/23-flash.md) |
| QSPI 플래시 | W25Q256JV 32 MB (OCTOSPI1), 4 바이트 주소, 메모리 맵 읽기 40 MB/s | [24](firmware/docs/24-qspi.md) |
| 폴트 기록 | 폴트 원인(CFSR / BFAR)과 레지스터를 남기고 리셋, 다음 부팅에 출력 | [25](firmware/docs/25-fault.md) |
| 부트로더 | 부팅 판정 → 앱 검증(TAG · CRC) → 점프. 머무르면 다운로드를 받는다 | [26](firmware/docs/26-bootloader.md) |
| 다운로드 | CLI 와 같은 포트로 패킷 프로토콜. UART 2 Mbps / USB CDC / USB HID, 앱이 돌고 있어도 된다 | [26](firmware/docs/26-bootloader.md), [27](firmware/docs/27-usb.md) |
| USB | TinyUSB CDC + HID. CDC 는 터미널(CLI)과 툴(다운로드)이 보율로 나눠 쓰고, HID 는 다운로드 전용 | [27](firmware/docs/27-usb.md) |
| 웹 업데이트 | GitHub Pages. 펌웨어는 WebHID / Web Serial, 부트로더는 WebUSB 로 ROM DFU | [28](firmware/docs/28-web.md) |

## 구성

```
index.html, web/          웹페이지 (GitHub Pages). web/bin/ 에 저장소 이미지
firmware/
├── docs/                 문서 (코어 / 확장보드 공통)
└── core/                 코어보드
    ├── stm32h5-boot/     부트로더 (0x0800_0000, 256 KB) + PC 툴
    └── stm32h5-fw/       코어보드 펌웨어 (0x0804_0400) + PC 툴
hardware/                 회로도 (코어보드, 확장보드 hg-swd-prog)
```

| 플래시 | 크기 | 내용 |
|---|---|---|
| `0x0800_0000` | 256 KB | 부트로더 |
| `0x0804_0000` | 1 KB | TAG (앱 크기 · CRC. 부트로더가 마지막에 쓴다) |
| `0x0804_0400` | ~447 KB | 앱 |

## 빠르게 시작하기

아래 [사용한 툴](#사용한-툴) 을 먼저 준비합니다.

```bash
# 부트로더 빌드 → ST-LINK(SWD) 로 기록 (한 번만)
cd firmware/core/stm32h5-boot
cmake -S . -B build; cmake --build build -j20
python3 tools/flash.py --target boot

# 앱 빌드 → 다운로드 후 실행
cd ../stm32h5-fw
cmake -S . -B build; cmake --build build -j20
python3 tools/download.py              # UART (ST-LINK VCP)
python3 tools/download.py --via cdc    # 또는 USB CDC / --via hid : USB HID
```

- 시리얼 터미널은 ST-LINK VCP 115200, 또는 보드 USB CDC 를 115200 으로 엽니다. `cli#` 에서 `help` 로 명령을 볼 수 있습니다
- VSCode 에는 빌드 / 쓰기 / 다운로드 태스크와 디버그 구성(`Debug FW` : 빌드 → 쓰기 → 앱 `main` 에서 멈춤)이 들어 있습니다
- 부트로더에 머무르려면 앱에서 `reset boot`, 또는 리셋 버튼을 빠르게 두 번 누릅니다

USB 는 pid.codes VID `0x1209` 를 씁니다. 부트로더 `1209:B563`, 앱 `1209:B565` ([27](firmware/docs/27-usb.md)).

## 사용한 툴

이 저장소의 빌드와 검증은 아래 버전으로 했습니다 (macOS 27 / Apple Silicon). 자세한 설치와 경로 규칙은 [10-dev-environment.md](firmware/docs/10-dev-environment.md) 에 있습니다.

| 툴 | 버전 | 용도 |
|---|---|---|
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | 15.3.1 (15.3.Rel1) | 컴파일러 · 링커 |
| CMake | 4.4.3 | 빌드 구성 |
| GNU Make | 3.81 | 빌드 (Windows 는 MinGW make) |
| STM32CubeCLT | 1.22.0 | 아래 ST 툴 묶음. 필요한 것만 추출해서 쓴다 |
| └ STM32CubeProgrammer (`STM32_Programmer_CLI`) | 2.23.0 | ST-LINK 로 기록 |
| └ ST-LINK gdbserver | 7.14.0 | 디버깅 |
| ST-LINK | V2-1, 펌웨어 V2J47M34 | SWD + VCP |
| STM32H5xx HAL / CMSIS | 1.5.0 | |
| TinyUSB | 0.18.0 | USB CDC + HID |
| Python | 3.9.6 | `flash.py` / `download.py` |
| pyserial / hidapi | 3.5 / 0.15.0 | UART · CDC / HID 다운로드 |
| VSCode | 1.139.1 | 편집 · 태스크 · 디버그 |
| └ Cortex-Debug (`marus25.cortex-debug`) | 1.12.1 | ST-LINK gdbserver 디버깅 |
| └ C/C++ (`ms-vscode.cpptools`) | 1.34.4 | IntelliSense |
| └ Firmware Task Manager (`baram-dev.firmware-task-manager`) | 1.1.4 | 태스크 트리, 시리얼 포트 선택 |

Windows 에서는 같은 툴의 Windows 판(CubeCLT 는 설치 프로그램)을 쓰고, 태스크는 `python` 으로 실행됩니다.

## 참고한 프로젝트

| 프로젝트 | 가져온 것 |
|---|---|
| stm32h5-w6300 | 같은 STM32H563 — 레이어 구조, 클럭, 플래시 · 리셋 드라이버, TinyUSB 구성 |
| NUCLEO-N657X0 | 문서 형식, CubeCLT 경로 규칙, 디버그 구성, uart / log / cli, 부트로더 다운로드 프로토콜 · 툴 |
| weact-h750-mini | USB ID 규칙 (pid.codes, 부트로더 / 앱 PID 분리) |

## 문서

[firmware/docs/README.md](firmware/docs/README.md) 에 전체 목차, 현재 상태, 다음 작업이 있습니다.

## 라이선스

[MIT](LICENSE). ST HAL / CMSIS, TinyUSB 는 각 파일에 적힌 라이선스를 따릅니다.
