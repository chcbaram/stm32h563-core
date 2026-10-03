# 21. UART + 로그 + CLI

> 디버그 커넥터 J1 의 UART 를 ST-LINK VCP 로 받아 부팅 로그와 CLI 를 띄운다.
> 이후 모든 기능은 여기서 로그로 상태를 보고 CLI 로 시험한다.
> 관련: [01-board.md](01-board.md#4-디버그-커넥터-j1-12505ws-06-6핀), [20-led.md](20-led.md)
>
> **상태: 보드에서 확인 (2026-10-03).** 부팅 배너, `help` / `uart info` / `log boot` / `md` 동작.

---

## 1. 하드웨어

| 항목 | 값 |
|---|---|
| 주변장치 | USART1, 115200 8N1 |
| 핀 | **PB15 = TX (U1_TXD)**, **PB14 = RX (U1_RXD)**, AF4 |
| 커넥터 | J1 5번 U1_TXD, 6번 U1_RXD |
| 호스트 | ST-LINK V2-1 VCP — `0483:3752`, 시리얼 `0673FF52…` (SWD 와 같은 장치) |

### TX / RX 를 맞바꿔 쓴다 (`UART_ADVFEATURE_SWAP`)

[01](01-board.md) 에서 확정하지 못했던 J1 의 방향이 여기서 풀렸다.

- STM32H563 의 AF 표상 USART1 AF4 는 **PB14 = TX, PB15 = RX** 다
- 보드는 **PB15 를 U1_TXD, PB14 를 U1_RXD** 로 배선했다 (회로도 표기 그대로 보드 기준 방향)
- 그래서 USART1 의 TX/RX 스왑 기능을 켠다

```c
uart_tbl[ch].p_huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
uart_tbl[ch].p_huart->AdvancedInit.Swap           = UART_ADVFEATURE_SWAP_ENABLE;
```

참고 프로젝트(`stm32h5-w6300`)도 같은 디버그 커넥터 설계라 같은 설정을 쓴다. 코드에는 원래 이유가
적혀 있지 않아 주석을 달았다. 스왑을 켠 상태에서 송수신이 모두 되는 것을 확인했다 (4절).
스왑을 끄고 안 되는 것까지는 시험하지 않았다.

### 시리얼 포트 찾기

ST-LINK 가 SWD 와 VCP 를 같이 낸다. 포트 이름은 USB 자리마다 바뀌므로 USB 시리얼 번호로 찾는다.

```bash
$ ioreg -l -w0 | grep -B60 'cu.usbmodem1412302"' | grep -E 'Vendor Name|Serial Number|idProduct'
  "USB Vendor Name" = "STMicroelectronics"
  "USB Serial Number" = "0673FF525784864967204317"     <- STM32_Programmer_CLI -l 의 SN 과 같다
  "idProduct" = 14162                                  <- 0x3752, ST-LINK V2-1 (VCP 있음)
```

`hardware/hg-swd-prog_V1.pdf` 는 UART 브리지가 아니라 **코어보드를 얹는 확장보드**다
(버튼 4, 부저, 1.3" OLED, microSD, SWD 출력 커넥터). 확장보드 펌웨어를 만들 때 따로 정리한다.

---

## 2. 구현

처음에는 w6300 것을 가져왔다가, **UART 다운로드에 필요한 기능이 있는 N6(`stm32n6-boot`) 것으로 바꿨다.**

| 파일 | 출처 | 차이 / 이유 |
|---|---|---|
| `src/hw/driver/uart.c` | N6 | 하드웨어 초기화(`uartInitHw`)만 H5 로: PB14/PB15 AF4, TX/RX 스왑, GPDMA non-secure, non-cacheable 구역 없음 |
| `src/hw/driver/log.c` | N6 | 링 버퍼(가득 차면 오래된 것부터 덮음), 줄마다 시각 `[  초.밀리초]` (RTC 가 맞으면 `[hh:mm:ss]`), `_write` 를 직접 갖는다 |
| `src/common/hw/src/cli.c`, `cli.h` | N6 | `cliSetRxFilter()` — 같은 UART 에서 cmd 패킷을 골라낸다 (다운로드에 필요) |
| `src/common/hw/include/{uart,log}.h` | N6 | 위 소스와 짝 |
| `src/common/core/{qbuffer,util_core}.{c,h}` | w6300 | 그대로 (CMake include 에 `src/common/core` 추가) |

N6 `uart.c` 가 w6300 것보다 나은 점:

| | w6300 | N6 |
|---|---|---|
| 보율 바꿔 다시 열기 | DeInit 후 전부 다시 | 수신 DMA 만 멈추고 UART 만 다시 (DMA 노드 설정 유지) — 다운로드 보율 올리기 |
| `uartClose()` | 플래그만 내림 | 원형 DMA 를 멈추고 UART 를 내린다 — 부트로더가 앱으로 점프하기 전에 필요 |
| 송신 타임아웃 | 100 ms 고정 | `100 + 길이 × 10 / 보율` ms — 115200 에서 1 KB 응답이 잘리지 않는다 |
| 수신 버퍼 | 1 KB | 4 KB — cmd 패킷(최대 1034 B)이 통째로 들어간다 |

### 수신은 GPDMA 원형 버퍼 + 폴링

인터럽트를 쓰지 않는다. `HAL_UART_Receive_DMA` 로 GPDMA1 채널 0 을 원형 리스트 모드로 돌리고,
`uartAvailable()` 이 남은 전송 수(`CBR1`)로 쓰기 위치를 계산한다.

```c
uart_tbl[ch].qbuffer.in = (uart_tbl[ch].qbuffer.len - uart_tbl[ch].p_huart->hdmarx->Instance->CBR1);
```

송신은 `HAL_UART_Transmit` (블로킹, 100 ms 타임아웃).

D-캐시는 켜져 있지만(DCACHE1) 이 칩의 DCACHE1 은 AHB 외부 메모리(FMC/OCTOSPI) 쪽 캐시이고
내부 SRAM 은 캐시되지 않는다. 그래서 N6 (`NUCLEO-N657X0/firmware/docs/21-uart-cli.md`) 와 달리
DMA 버퍼의 캐시 일관성 처리가 필요 없다. ⚠️ RM 으로 다시 확인할 것.

### `hw_def.h`

```c
#define _USE_HW_UART
#define      HW_UART_MAX_CH         1          // 이후 USB CDC 를 채널 2 로 꽂으며 2 ([27](27-usb.md))
#define      HW_UART_CH_SWD         _DEF_UART1    // 디버그 커넥터 J1 (ST-LINK VCP)
#define      HW_UART_CH_CLI         HW_UART_CH_SWD

#define _USE_HW_CLI
#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_SWD

#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_UART            1
```

`HAL_UART_MODULE_ENABLED` 를 다시 켰다.

### 초기화와 메인 루프

`hwInit()` 은 참고 프로젝트와 같은 순서로 `cliInit → logInit → ledInit → uartInit → uartOpen → logOpen` 후
부팅 배너를 찍는다. `apMain()` 은 LED 토글과 `cliMain()` 만 돈다.
참고 프로젝트의 모듈 시스템(`MODULE_DEF`, `moduleUpdate`)은 기능이 늘어날 때 들여온다.

`printf` 는 N6 `log.c` 의 `_write` 가 로그 채널(VCP)로 보낸다. `syscalls.c` 의 weak `_write` 는 버리는 그대로 둔다.

---

## 3. 크기

| | LED 단계 | 이번 |
|---|---|---|
| FLASH | 8,064 B | **65,200 B** |
| RAM | 1,632 B | **15,016 B** (N6 uart 수신 버퍼 4 KB) |

(그때는 `-O0`. 지금은 `-Og` — [26](26-bootloader.md) 2절) FLASH 가 크게 는 것은 `-O0` 으로 빌드한 HAL RCC 확장 함수들 때문이다 (UART 클럭 설정이 끌어온다).

| 심볼 | 크기 |
|---|---|
| `HAL_RCCEx_GetPeriphCLKFreq` | 9,796 B |
| `HAL_RCCEx_PeriphCLKConfig` | 7,768 B |
| `_strtod_l` (CLI `getFloat` 의 `strtof`) | 3,040 B |
| `HAL_RCC_OscConfig` | 2,160 B |

RAM 은 로그 버퍼(boot 2 KB + list 4 KB), CLI 노드(1.7 KB), UART 수신 버퍼(4 KB) 다. 지금은 신경 쓸 크기가 아니다.

---

## 4. 검증

baram-term 이 VCP 를 열어 두고 있어서 포트를 직접 열지 않고 `baram-ctl` 로 보내고 읽었다.

### 부팅 배너

SWD 로 쓰고 리셋한 직후:

```
[ Firmware Begin... ]
Booting..Name 		: STM32H5-FW
Booting..Ver  		: V261003R1
Booting..Clock		: 250 Mhz
Booting..Date 		: Oct  3 2026
Booting..Time 		: 21:39:40
Booting..Addr 		: 0x8000000
```

TX 확인. 프롬프트 `cli# ` 는 첫 입력을 받은 뒤에 나온다 (CLI 구현이 그렇다).

### CLI

```
cli# help
---------- cmd list ---------
HELP
MD
LOG
UART
-----------------------------

cli# uart info
_DEF_UART1 : USART1 SWD   , 115200 bps

cli# log boot
[    0.002]
[ Firmware Begin... ]
[    0.004]	Booting..Name 		: STM32H5-FW
[    0.007]	Booting..Ver  		: V261003R1
...

cli# md 0x08000400 4
    0x08000400:  0x56455220 0x31363256 0x52333030 0x00000031  | REVV261003R1...|
```

- 명령이 응답한다 → **RX 확인**
- `md 0x08000400` 이 `.version` 영역(매직 `"VER "` + 버전 문자열)을 보여 준다 → 링커 배치와 일치

---

## 5. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `qbuffer.h: No such file or directory` | 새로 들인 `src/common/core` 가 include 경로에 없었다 | CMake `target_include_directories` 에 추가 |
| 부팅 직후 프롬프트가 안 보임 | 고장이 아니다. CLI 가 입력을 받은 뒤에 프롬프트를 찍는다 | — |

---

## 6. 다음

- [x] fault 핸들러 — 폴트 원인과 PC 를 로그로 → [25](25-fault.md)
- [x] reset 원인 / RTC 백업 레지스터 → [22](22-rtc-reset.md)
- [x] 모듈 시스템 → [26](26-bootloader.md) (swtimer 는 아직)
