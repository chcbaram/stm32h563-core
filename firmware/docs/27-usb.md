# 27. USB CDC + HID (TinyUSB) — CLI 와 다운로드

> 보드의 USB-C 로 CLI 를 쓰고, CDC 나 HID 로 앱을 내려받는다. 부트로더와 앱이 **같은 TinyUSB 코드**를 쓴다.
> w6300 부트로더의 TinyUSB(MSC + CDC + HID) 에서 MSC 를 뺐다. cmd 채널 구조는 w6300 의 CDC / HID 채널을 N6 모양으로 옮겼다.
> 관련: [21](21-uart-cli.md) (cli RX 필터), [26](26-bootloader.md) (부트로더 / 다운로드)
>
> **상태: 보드에서 확인 (2026-10-03).** CDC CLI, CDC / HID 다운로드, CDC 를 CLI 로 쓰는 중 HID 업데이트.

---

## 1. 쓰는 법

```bash
cd firmware/core/stm32h5-fw
python3 tools/download.py              # UART (ST-LINK VCP, 기본)
python3 tools/download.py --via cdc    # USB CDC  (0.46 s)
python3 tools/download.py --via hid    # USB HID  (1.85 s). 터미널이 CDC 를 CLI 로 쓰는 중에도 된다
```

| VSCode 태스크 (앱) | |
|---|---|
| `download-uart` / `download-uart (포트 선택)` | UART |
| `download-cdc` | USB CDC |
| `download-hid` | USB HID (hidapi 필요 : `pip install hidapi`) |

CLI 는 보드의 USB CDC 포트를 **115200** 으로 열면 된다 (`/dev/cu.usbmodem…`, Windows 는 COMx).

---

## 2. 왜 TinyUSB 로 둘 다

w6300 은 부트로더가 TinyUSB, 앱이 ST USB Device Library(composite) 였다. 앱은 이미 ST 스택 CDC 가 돌고 있어서 회귀 범위를 줄이려고 그렇게 했고,
그 대신 ST composite 에서 PMA 배치, `USBD_static_malloc` 공유, `classId` 순서, IAD 누락 같은 함정을 여럿 밟았다 (w6300 docs 15).

여기는 처음부터 만드는 것이라 **부트로더·앱 모두 TinyUSB 0.18** 로 했다. usb / cdc / hid 드라이버 파일이 두 프로젝트에서 같고,
나중에 부트로더에 MSC(UF2) 를 붙이기도 쉽다.

| 파일 | 출처 | 바꾼 것 |
|---|---|---|
| `src/lib/tinyusb/` | w6300 부트로더 (0.18) | 그대로 |
| `hw/driver/usb/tusb_config.h` | w6300 | `CFG_TUD_MSC 0` |
| `hw/driver/usb/usb_desc.c` | w6300 | MSC 인터페이스 / 엔드포인트 삭제, VID/PID, 문자열 |
| `hw/driver/usb/usb.c`, `usb_hid.c` | w6300 | `log.h` include, HID 수신을 `drv_hid.c` 로 |
| `hw/driver/cdc.c`, `cdc.h` | w6300 | CDC 를 uart 채널로 쓰는 `cdc_uart_driver` 추가 |
| `ap/modules/common/usb/usb_task.c` | 새로 | USB 모듈 (`usbInit` + `tud_task`) |
| `ap/modules/cmd/driver/drv_usb.c`, `drv_hid.c` | w6300 → N6 `cmd_driver_t` 모양 | `drvUsbInit() / drvHidInit()` 가 함수 포인터를 채운다 |
| `ap/modules/cmd/cmd_task.c` | N6 + w6300 | UART / CDC / HID 세 채널 |
| `ap/modules/common/cli/cli.c` | N6 + w6300 `cli_mgr` | CLI 포트 전환 (3 절) |
| `ap/modules/cmd/driver/drv_uart.c` | N6 | CLI 가 CDC 로 옮겨 가도 UART cmd 가 되게 (4 절) |

### 장치

| 인터페이스 | 엔드포인트 | |
|---|---|---|
| ITF0 CDC Comm | `0x81` notif | |
| ITF1 CDC Data | `0x02` OUT / `0x82` IN (64 B) | CLI 또는 cmd |
| ITF2 HID | `0x03` OUT / `0x83` IN (64 B, bInterval 1) | cmd 전용. usage page `0xFF00` (vendor) |

| VID:PID | 모드 | 제품명 |
|---|---|---|
| `1209:B563` | 부트로더 CDC + HID | `STM32H5-BOOT` |
| `1209:B564` | 부트로더 CDC + HID + MSC (UF2 를 넣을 때. 아직 없다) | |
| `1209:B565` | 앱 CDC + HID | `STM32H5-FW` |
| `1209:B566` | 앱 CDC + HID + MSC (QSPI / 확장보드 SD 를 드라이브로. 아직 없다) | |

```
HID 1209:B565 APP      STM32H5-FW
CDC 1209:B565 /dev/cu.usbmodem1412101 STM32H5-FW
```

- VID `0x1209` 는 pid.codes (오픈소스 하드웨어용 공용 VID). 처음에는 TinyUSB 예제 VID `0xCAFE:0x4563` 이었다 (6 절)
- **부트로더와 앱의 PID 를 나눈다** (weact-h750 과 같다). 연결하기 전에 PID 로 모드를 알고, 붙은 뒤에는 INFO 의 `mode` 로도 안다
- PID 는 `usb_desc.c` 가 `HW_DEV_MODE` 로 고른다. 두 프로젝트의 USB 파일이 같은 내용으로 유지된다
- 시리얼은 칩 UID. 부트로더와 앱이 같아서 **CDC 포트 이름이 리셋 뒤에도 같다** (PID 가 바뀌어도 macOS 에서 같았다)
- 부트로더는 USB 를 모듈로 열어 **머무를 때만** 열거된다. 앱으로 점프하기 전에 `usbDeInit()` 으로 내린다

---

## 3. CDC 의 주인 — 호스트가 연 보율로 가른다

CLI 와 cmd 가 한 CDC 스트림을 각자 읽으면 서로 바이트를 훔쳐 둘 다 깨진다 (w6300 docs 15 함정 5).
**호스트가 연 보율로 주인을 가른다** (w6300 / weact 와 같다).

| 호스트가 연 보율 | CDC 의 주인 | |
|---|---|---|
| **115200** | CLI | 터미널. cli 모듈이 CLI 포트를 CDC 로 옮기고 로그도 따라간다. cmd 는 CDC 를 건너뛴다 |
| 그 외 (`download.py` 는 921600) | cmd | 호스트 툴. CLI 는 UART 에 남는다 |
| 닫힘 | — | CLI 는 UART 로 돌아온다 |

CDC 는 uart 채널 `HW_UART_CH_USB`(`_DEF_UART2`) 로 꽂는다 (`uartSetDriver(HW_UART_CH_USB, &cdc_uart_driver)`).
N6 `uart.c` 의 드라이버 훅이라 cli / log 는 uart 함수만 쓰면 된다.

```
cli# uart info
_DEF_UART1 : USART1 SWD   , 115200 bps
_DEF_UART2 : USB CDC      , 115200 bps
```

HID 는 전용 채널이라 이 판정과 관계없이 **언제나** cmd 를 받는다.

---

## 4. CLI 가 CDC 로 옮겨 가면 UART cmd 는?

UART 의 cmd 패킷은 cli 가 자기 포트를 읽을 때 RX 필터로 골라낸다 ([21](21-uart-cli.md)). CLI 가 CDC 로 옮겨 가면

- UART 는 아무도 읽지 않게 되고
- 거꾸로 필터가 **CDC 로 온 바이트**를 UART cmd 큐로 가져가 응답이 UART 로 나간다

`drv_uart.c` 를 이렇게 고쳤다.

| | |
|---|---|
| `drvUartCliFilter()` | cli 에 거는 필터. **cli 포트가 이 UART 일 때만** 가져간다 |
| `drvUartUpdate()` | cli 포트가 다른 곳이면 UART 를 **직접 읽어** 필터에 넣는다 (cmd 가 아닌 바이트는 버린다) |

---

## 5. 측정과 검증

### 다운로드 (83 KB 앱, `-Og`)

| 방식 | 쓰기 | 합계 |
|---|---|---|
| UART 2 Mbps | 104.6 KB/s | 0.86 s |
| **USB CDC** | **209.5 KB/s** | **0.46 s** |
| USB HID | 46.9 KB/s | 1.85 s |

HID 가 느린 것은 64 B 리포트에 63 B 씩 나눠 보내고 패킷마다 응답을 기다리기 때문이다.
셋 다 앱이 돌고 있는 상태에서 시작했고, 앱이 부트로더로 리셋 → **USB 가 다시 열거되면 툴이 다시 찾아 붙고** → 쓰기 → 앱으로 점프했다.

### CDC 를 CLI 로 쓰면서 HID 로 업데이트

CDC 를 115200 으로 열어 둔 "터미널"(끊기면 다시 붙는 스크립트) 이 본 것:

```
<open 22:45:27>
cli#                                          <- 앱 CLI
<lost 22:45:29: SerialException>              <- HID 로 FW_UPDATE → 앱이 부트로더로 리셋
<open 22:45:30>
[  ] fw begin 84840 bytes                     <- 부트로더 CDC 에 다시 붙음. HID 로 쓰는 중의 로그
cli# [  ] fw end 84840 bytes (rx 84840), crc 0xF216
[  ] jump : 0x0804384D
<lost 22:45:32: SerialException>              <- 앱으로 점프 (USB 재열거)
<open 22:45:33>
cli#                                          <- 앱 CLI
```

터미널은 업데이트 중 두 번(부트로더로, 앱으로) 잠깐 끊긴다. baram-term 은 자동으로 다시 붙는다.

### CLI (CDC 115200)

```
cli#
uart info
_DEF_UART1 : USART1 SWD   , 115200 bps
_DEF_UART2 : USB CDC      , 115200 bps
```

### 크기 (`-Og`)

| | USB 전 | USB 후 |
|---|---|---|
| 부트로더 | 70.3 KB | 86.3 KB / 254 KB (34%) |
| 앱 | 66.9 KB | 82.8 KB |

TinyUSB CDC + HID 로 약 16 KB 늘었다.

---

## 6. 웹에서 쓸 때 — VID/PID 와 시리얼 번호

웹페이지(GitHub Pages) 로 업데이트할 계획이 있어 보드를 무엇으로 알아볼지 정리했다.

| API | `requestDevice` 필터 | 시리얼 번호 |
|---|---|---|
| WebHID (HID 업데이트) | vendorId, productId, usagePage, usage | 볼 수 없다 (`HIDDevice` 에 없음) |
| Web Serial (CDC 업데이트) | usbVendorId, usbProductId | 볼 수 없다 (`getInfo()` 는 VID/PID 만) |
| WebUSB (ROM DFU) | VID, PID, class, serialNumber | 볼 수 있다 |

(API 사양을 알고 있는 대로 적었다. 웹페이지를 만들 때 실제로 확인한다)

→ WebHID / Web Serial 은 시리얼 번호로 거를 수 없다. **보드 인식은 VID/PID 로 한다.** 시리얼은 칩 UID 그대로 둔다 (여러 보드 구분, 포트 이름 유지).

### PID 고르기 — 다른 프로젝트와 겹치지 않게

내 저장소들에서 pid.codes VID 를 쓰는 것은 weact-h750-mini 하나였다.

| PID | 프로젝트 | pid.codes 등록 |
|---|---|---|
| `1209:B750` / `B751` / `B752` | weact-h750 (부트로더 / 부트로더 + MSC / 앱) | **미등록** |
| `1209:B563` / `B564` / `B565` | 이 보드 | **미등록** (비어 있음을 확인) |

레지스트리(`github.com/pidcodes/pidcodes.github.com`)는 `1209/<PID>/index.md` 로 확인했다 (`gh api .../contents/1209/B563` → 404, 이미 등록된 `B010`, `B747` 은 파일이 나온다).
디렉터리 목록은 API 가 1000 개까지만 돌려줘서 판단에 쓰지 않았다.

→ **pid.codes 에 PR 로 등록해 두는 것을 권한다** (weact 의 B750 ~ B752 도 함께). 등록 전에는 남이 먼저 가져갈 수 있다.

웹 필터에는 세 PID 를 다 넣는다 (WebHID / Web Serial 모두 필터 배열을 받는다).
확장보드는 같은 부트로더라 PID 를 나누기보다 INFO 의 `name` / USB 제품명으로 가른다.

바꿀 곳 : 펌웨어 `usb_desc.c` 의 `USB_VID / USB_PID_*`, 툴 `cmdproto.py` 의 `USB_VID / USB_PID_*`.

## 7. MSC — 정해 둔 것 (아직 구현하지 않는다)

| | 결정 | 이유 |
|---|---|---|
| 부트로더 MSC (UF2 드래그 & 드롭) | **요청할 때만** 보인다. 리셋 더블클릭 / 앱의 요청(`MODE_BIT_MSC`) 으로 들어왔을 때 PID `B564` 로 열거. 그 외 부트로더는 `B563` (CDC + HID) | 부트로더에 들어갈 때마다 드라이브가 마운트되면 번거롭고, 꺼낼 때 OS 가 경고한다. weact-h750 과 같은 방식 (w6300 은 항상 보였다) |
| 앱 MSC | 나중에. 저장소를 갈아끼울 수 있게 (QSPI 32 MB 또는 확장보드의 microSD) | FAT 와 동시 쓰기(호스트와 펌웨어가 같이 쓰면 깨진다) 설계가 따로 필요하다 |
| MSC 유무로 PID 를 바꾼다 | B563 ↔ B564, B565 ↔ B566 | 인터페이스 구성이 바뀌는데 PID 가 같으면 Windows 가 캐시한 디스크립터로 잘못 붙는다 |
| UF2 family ID | `0xFFFF0005` (후보) | 내 저장소에서 쓰는 것: `0001` ~ `0004`, `0010` ~ `0015` (convex). `0005` 는 UF2 와 무관한 상수로만 나온다 |

UF2 를 넣을 때 참고할 곳 : weact-h750 부트로더 `ap/modules/uf2/` (단일 영역, FAT16 16 MB, MSC 유무로 디스크립터·PID 를 런타임에 고른다).
내장 플래시에 맞출 것 — 지우기 단위 8 KB 섹터(FIRM 기준 격자), TAG 는 첫 섹터 안이라 첫 블록이 올 때 섹터째 지우고 마지막에 쓰기만.

## 8. 남은 것

- [x] VID/PID — `1209:B563` (부트로더) / `B565` (앱)
- [ ] pid.codes 에 PR 로 등록 (사용자)
- [ ] Windows 에서 CDC / HID (드라이버 없이 붙는지)
- [ ] 웹페이지 — WebHID / Web Serial 업데이트, WebUSB ROM DFU 로 부트로더 업데이트
- [ ] 부트로더 MSC (UF2) — 7 절 결정대로
- [ ] 앱 MSC (QSPI / SD)
