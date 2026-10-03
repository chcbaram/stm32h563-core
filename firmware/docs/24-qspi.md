# 24. QSPI 플래시 (W25Q256JV, OCTOSPI1)

> 보드의 32 MB 외부 NOR. 부트로더와 앱이 함께 쓴다 (리소스, 다운로드 버퍼, 로그 등).
> `flash.c` 가 주소로 내장 플래시와 QSPI 를 가른다 (N6 와 같은 모양).
> 관련: [01-board.md](01-board.md#6-qspi-플래시-u3-w25q256jv-32-mb), [23-flash.md](23-flash.md)
>
> **상태: 보드에서 확인 (2026-10-03).** 인식, 4 바이트 주소, 지우기 / 쓰기 / 읽기 비교, 메모리 맵(XIP) 40 MB/s, XIP 중 쓰기.

---

## 1. 하드웨어

| 항목 | 값 |
|---|---|
| 칩 | U3 W25Q256JVEIQ — 32 MB, JEDEC `EF 40 19`. `IQ` 는 QE=1 출하 |
| 주변장치 | OCTOSPI1 (H5 HAL 이름은 `XSPI`), quad SPI, 메모리 맵 창 `0x9000_0000` |
| 직렬 저항 | 데이터 라인마다 27Ω (R2~R6). CS 는 R7 10K 풀업 |

| 신호 | 핀 | AF |
|---|---|---|
| CLK | PA3 | 3 |
| NCS | PB10 | 9 |
| IO0 | PC3 | 9 |
| IO1 | PB0 | **6** |
| IO2 | PC2 | 9 |
| IO3 | PA1 | 9 |

AF 는 같은 칩을 쓰는 `Arduino_Core_STM32` 의 `variants/STM32H5xx/H563R(G-I)T_H573RIT/PeripheralPins.c` 로 확인했다.
IO1(PB0) 만 AF6 이다.

---

## 2. 구현

`stm32h7r-mini` 의 `qspi.c` (XSPI HAL, W25Q128JV) 를 바탕으로 했다. 명령 구성, WriteEnable, AutoPolling, 메모리 맵 흐름은 같다.

### 4 바이트 주소 전용 명령

32 MB 라 16 MB 를 넘는 주소는 4 바이트가 필요하다. 참고 드라이버들은 모두 16 MB 이하라 3 바이트만 쓴다.
`stm32h5-bd` (같은 H5 + W25Q256JV, SPI) 는 `ENTER_4BYTE_MODE`(0xB7) 로 칩을 4 바이트 모드로 바꾸고 일반 명령을 쓴다.

여기서는 칩의 주소 모드에 기대지 않는 **4 바이트 전용 명령**을 쓴다. 칩이 3 바이트 모드로 켜지든(ADP=0 기본) 리셋 명령(0x99) 으로
돌아가든 같은 명령이 통한다.

| 동작 | 명령 | 주소 | 데이터 |
|---|---|---|---|
| 읽기 | **0xEC** Fast Read Quad I/O (4B) | 4 선, 32 비트 + 모드 바이트 + 더미 4 | 4 선 |
| 페이지 쓰기 (256 B) | **0x34** Quad Input Page Program (4B) | 1 선, 32 비트 | 4 선 |
| 4 KB 지우기 | **0x21** Sector Erase (4B) | 1 선, 32 비트 | — |
| 64 KB 지우기 | **0xDC** Block Erase (4B) | 1 선, 32 비트 | — |
| 상태 | 0x05 SR1 (WIP / WEL) | — | 1 선 |

### 참고 드라이버에서 고친 것

| 참고 드라이버 | 문제 | 여기 |
|---|---|---|
| `qspiHwGetStatus()` 가 0x70 (Flag Status Register) | Micron 명령. Winbond 에는 없다 | SR1 (0x05) 의 WIP |
| `qspiWrite / Erase` 가 `if (qspiGetXipMode() == false) return false;` | 조건이 뒤집혀 XIP 가 **꺼져 있을 때** 쓰기·지우기를 거부한다 | XIP 이면 잠깐 풀고 쓴 뒤 되돌린다 |
| `qspiRead` 가 XIP 일 때 `memcpy(p_data, (void *)addr, ...)` | 오프셋을 절대 주소처럼 쓴다 | `QSPI_XIP_ADDR + addr` |
| 메모리 맵 진입 전에 `hqspi.State = HAL_XSPI_STATE_CMD_CFG` 로 덮어씀 | HAL 상태를 억지로 바꾼다 | 쓰기 명령(WRITE_CFG)도 정식 설정 (3 절) |
| 명령마다 구조체 필드 15 줄 반복 | | `qspiCmdInit()` 으로 기본값을 채우고 필요한 필드만 바꾼다 |

API 의 주소는 **칩 안 오프셋**(0 ~ 32 MB) 이다. 메모리 맵 주소로 쓰려면 `flash.c` 를 거친다 (4 절).

`qspiErase()` 는 4 KB 경계로 넓히고, 64 KB 로 정렬된 구간은 64 KB 명령으로 지운다.

### 클럭

| | 값 |
|---|---|
| 커널 클럭 | HCLK 250 MHz (`RCC_OSPICLKSOURCE_HCLK`) |
| 프리스케일러 | 2 → **83 MHz** (칩 규격 133 MHz. 27Ω 직렬 저항이 있어 여유를 두었다) |
| 샘플링 | 반 클럭 지연 (`HAL_XSPI_SAMPLE_SHIFT_HALFCYCLE`) |
| `MemorySize` | `HAL_XSPI_SIZE_256MB` — 이름과 달리 **256 Mbit = 32 MB** (주소 25 비트) |

`hw_def.h` 에는 `_USE_HW_QSPI` 만 둔다. 메모리 맵 주소(`OCTOSPI1_BASE`)와 칩 정보는 드라이버 안에 있다.

---

## 3. 메모리 맵(XIP)

### `HAL_XSPI_MemoryMapped()` 는 읽기·쓰기 명령이 둘 다 설정돼야 한다

처음에 읽기 명령(READ_CFG)만 설정했더니 `qspi xip on` 이 실패했다. HAL 상태가 `READ_CMD_CFG` 에 머물고,
`HAL_XSPI_MemoryMapped()` 는 `CMD_CFG`(읽기 + 쓰기 둘 다 설정) 에서만 진행한다.
참고 드라이버는 상태를 덮어써서 넘겼다. 여기서는 쓰기 명령(0x34, WRITE_CFG) 도 설정한다. 메모리 맵으로 쓰지는 않는다.

> 이때 실패한 상태로 `md 0x91000000` 을 쳐서 매핑되지 않은 주소를 읽었고, **폴트가 나서 보드가 멈췄다**.
> 그때 폴트 핸들러는 무한루프라 원인을 남기지 못했다. → [25](25-fault.md) 에서 고쳤다 (같은 `md 0x91000000` 이 이제 `BFAR 0x91000000` 으로 남는다).

### DCACHE1 이 메모리 맵 영역을 캐시한다 — 쓰기 뒤에 무효화

H5 의 DCACHE1 은 바로 이 외부 메모리 영역(0x9000_0000~) 용 캐시다 ([21](21-uart-cli.md) 에서 내부 SRAM 은 캐시되지 않는다고 한 그 캐시).
XIP 로 읽어 캐시에 올라간 줄은 명령으로 고쳐 써도 남아 있어 옛 값이 보인다.

```
cli# qspi xip on
cli# md 0x91000040 4                    <- 캐시에 올라간다
    0x91000040:  0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF
cli# qspi write 0x1000044 0xDEADBEEF     <- XIP 를 잠깐 풀고 명령으로 쓴다
cli# md 0x91000040 4
    0x91000040:  0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF   <- 옛 값 (고치기 전)
```

쓰기·지우기 뒤(`qspiXipResume()`)에 DCACHE1 을 통째로 무효화한다. 지금 XIP 가 아니어도 이전 XIP 때 올라간 줄이 남아 있을 수 있어 항상 비운다.

```
cli# md 0x91000080 4
    0x91000080:  0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF
cli# qspi write 0x1000084 0xDEADBEEF
cli# md 0x91000080 4
    0x91000080:  0xFFFFFFFF 0xDEADBEEF 0xFFFFFFFF 0xFFFFFFFF   <- 고친 뒤
cli# qspi erase 0x1000000 4096
cli# md 0x91000080 4
    0x91000080:  0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF
```

---

## 4. `flash.c` 에서 QSPI 쓰기 (주소로 가른다)

N6 `flash.c` 처럼 주소로 대상을 고른다. 부트로더 / 다운로드는 `flashRead / Write / Erase` 만 부르면 된다.

| 주소 | 대상 |
|---|---|
| `0x0800_0000` ~ `0x081F_FFFF` | 내장 플래시 (`flashIntErase / Write / Read`, [23](23-flash.md) 의 검사 그대로) |
| `0x9000_0000` ~ `+32 MB` | QSPI. `qspiGetAddr()` 를 빼서 칩 오프셋으로 넘긴다 |

```
cli# flash erase 0x91000000 4096
erase OK : 37 ms
cli# flash write 0x91000000 0x55667788
cli# flash read 0x91000000 16
0x91000000 : 88 77 66 55 FF FF FF FF FF FF FF FF FF FF FF FF
cli# flash info
...
QSPI    : 0x90000000 32 MB (OK)
```

QSPI 쪽은 내장 플래시와 달리 빈 자리 검사를 하지 않는다. NOR 는 1 → 0 으로만 바뀌어 다시 써도 ECC 같은 문제가 없다 (값은 AND 가 된다).

---

## 5. 검증

### 인식

```
[OK] qspiInit()
     W25Q256JV Found
     CLK  : 83 Mhz
     ADDR : 0x90000000
     SIZE : 32 MB
```

### 지우기 → 패턴 쓰기 → 읽어 비교 (`qspi test`)

패턴은 `주소 ^ 0xA5A5A5A5` 라 위치마다 다르다.

| 위치 | 결과 | 지우기 | 쓰기 | 읽기 (명령) |
|---|---|---|---|---|
| 0x0000000 (64 KB) | OK, err 0 | 168 ms | 119 ms | 24 ms |
| 0x1000000 (16 MB 위) | OK, err 0 | 157 ms | 120 ms | 23 ms |
| 0x1FF0000 (끝 64 KB) | OK, err 0 | 163 ms | 120 ms | 24 ms |

### 4 바이트 주소가 실제로 쓰이는지

3 바이트로 잘리면 0x1000000 은 0x000000 과 같은 자리가 된다. 둘 다 지우고 위에만 썼다.

```
cli# qspi write 0x1000000 0xCAFEBABE
cli# qspi read 0x0 8
0x00000000 : FF FF FF FF FF FF FF FF          <- 겹치지 않는다
cli# qspi read 0x1000000 8
0x01000000 : BE BA FE CA FF FF FF FF
```

### 속도

| 읽기 | 속도 |
|---|---|
| 명령 (`HAL_XSPI_Receive`, 512 B 단위) | 2,876 KB/s |
| **메모리 맵(XIP)** | **40,960 KB/s** — 83 MHz × 4 bit = 41.6 MB/s 에 거의 붙는다 |

### 간접(명령) 방식이 느린 이유

버스나 칩이 아니라 **CPU 가 바이트마다 HAL 루프를 돌기 때문**이다. `HAL_XSPI_Receive()` 는 바이트 하나마다

1. `XSPI_WaitFlagStateUntilTimeout()` 을 부른다 (SR 읽기 + `HAL_GetTick()` 타임아웃 검사)
2. 데이터 레지스터를 8 비트로 한 번 읽는다

그리고 HAL 은 (그때) `-O0` 으로 빌드됐다 (CMake `SRC_FILES_SPECIAL`, 지금은 `-Og`). 2.9 MB/s 는 250 MHz 에서 바이트당 약 86 클럭이다.
선로는 41.6 MB/s 를 낼 수 있으니 대부분 놀고 있다.

HAL 만 잠깐 `-O2` 로 빌드해 쟀다 (재고 나서 `-O0` 으로 되돌렸다).

| HAL 빌드 | 간접 읽기 | 64 KB 읽기 | 64 KB 쓰기 |
|---|---|---|---|
| `-O0` (처음) | 2,876 KB/s | 24 ms | 119 ms |
| **`-Og` (지금, [26](26-bootloader.md) 에서 바꿈)** | 4,063 KB/s | 16 ms | 113 ms |
| `-O2` (시험) | 9,660 KB/s (3.4 배) | 8 ms | 104 ms |
| XIP | 40,960 KB/s | — | — |

- 읽기는 컴파일 옵션만으로 3.4 배. 그래도 바이트마다 함수와 틱 검사가 남아 XIP 의 1/4 이다
- 쓰기는 거의 그대로다. 256 B 페이지마다 칩의 프로그램 시간(약 0.4 ms)을 기다리는 것이 대부분이다 (칩이 정한다)

큰 읽기는 XIP 로 한다 (XIP 일 때 `qspiRead` 는 memcpy). 간접 방식을 빠르게 하려면 GPDMA 로 받는다 — 재 보지 않았다.

---

## 6. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `qspi xip on` → Fail, 이후 `qspiRead` 도 Fail | `HAL_XSPI_MemoryMapped()` 가 READ_CFG + WRITE_CFG 를 모두 요구 | 쓰기 명령도 설정 |
| 그 상태에서 `md 0x91000000` → 보드 멈춤 | 매핑 안 된 주소 읽기 → 폴트 → 무한루프 핸들러 | [25](25-fault.md) 폴트 기록 |
| XIP 중 쓴 값이 안 보임 | DCACHE1 에 옛 줄이 남음 | 쓰기·지우기 뒤 DCACHE1 무효화 |

---

## 7. 남은 것

- [ ] 클럭을 올려 보기 (125 MHz = 프리스케일러 1). 27Ω 와 반 클럭 샘플링으로 되는지
- [ ] 큰 범위(수 MB) 비교 시험
- [ ] 칩 전체 지우기 시간 (`qspiEraseChip`, 규격 최대 250 s 대)
