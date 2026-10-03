# 23. 내장 플래시 드라이버

> 부트로더가 앱을 쓸 때, 앱이 나중에 슬롯·설정을 쓸 때 쓰는 드라이버다. 부트로더보다 먼저 앱에서 검증했다.
> 관련: [11-project-skeleton.md](11-project-skeleton.md) (링커), [22-rtc-reset.md](22-rtc-reset.md)
>
> **상태: 보드에서 확인 (2026-10-03).** 지우기 / 쓰기 / 읽기, 재기록 거부, 보호 영역 거부.
>
> 이후 `flash.c` 가 주소로 QSPI 까지 가르게 됐다 (`0x9000_0000~`) → [24-qspi.md](24-qspi.md#4-flashc-에서-qspi-쓰기-주소로-가른다).
> 아래 내장 플래시 함수는 `flashIntErase / Write / Read` 로 이름이 바뀌었다.

---

## 1. STM32H563 내장 플래시

| 항목 | 값 |
|---|---|
| 크기 | 2 MB = 뱅크 2 개 × 1 MB |
| 섹터 | **8 KB** × 128 / 뱅크 |
| 프로그램 단위 | **16 B (쿼드워드)** |
| 재기록 | **불가.** 이미 쓴 쿼드워드를 다시 쓰면 HAL 은 OK 를 돌려주지만 ECC 가 깨지고, 그 워드를 읽는 순간 NMI 가 난다 (w6300 실기 확인) |
| 동시 접근 | 한 뱅크를 지우거나 쓰는 동안 다른 뱅크에서 코드를 읽을 수 있다 (RWW) |

---

## 2. 출처와 바꾼 것

w6300 의 `flash.c` 를 그대로 가져왔다 (w6300 앱과 부트로더의 것이 같다).

| 바꾼 것 | 이유 |
|---|---|
| `#include "log.h"` | 이 프로젝트 `bsp.h` 는 `logPrintf` 를 선언하지 않는다 ([22](22-rtc-reset.md) 의 `reset.c` 와 같다) |
| CLI `flash info` 의 영역 표 | 슬롯 / BOOT_LOG / NVS 는 아직 없다. BOOT / FIRM / PROTECT 만 찍는다 |

드라이버가 지키는 것:

| 검사 | 동작 |
|---|---|
| 범위 | 0x0800_0000 ~ 0x081F_FFFF 밖이면 거부 |
| 보호 영역 | `FLASH_PROTECT_ADDR / SIZE` 와 겹치면 지우기·쓰기 거부. 오프바이원 하나가 벽돌로 이어지는 구간이라 마지막 방어선으로 둔다 |
| 정렬 | 쓰기 시작 주소가 16 B 정렬이 아니면 거부 (read-modify-write 를 하지 않는다) |
| 빈 자리 | 쓰기 전에 대상 쿼드워드가 0xFF 인지 확인. 아니면 거부 (재기록 → ECC 깨짐 방지) |
| 꼬리 | 길이가 16 의 배수가 아니면 마지막 쿼드워드의 나머지를 0xFF 로 채운다 |
| 지우기 뱅크 | `FLASH_EraseInitTypeDef.Banks` 를 반드시 채운다. 비워 두면 스택 쓰레기값에 따라 엉뚱한 뱅크가 지워질 수 있다 |
| SWAP_BANK | 옵션 바이트의 뱅크 스왑이 켜져 있으면 논리 뱅크를 뒤집어 계산한다 (지금은 꺼져 있다) |

---

## 3. 레이아웃 (`hw_def.h`)

부트로더를 붙일 때 쓸 주소를 미리 정했다. w6300 과 같다.

```c
#define FLASH_ADDR_BOOT             0x08000000      // 128 KB  부트로더 자리 (지금은 이 앱이 여기서 돈다)
#define FLASH_ADDR_FIRM             0x08020000      // 448 KB  TAG 1 KB + 앱
#define FLASH_ADDR_FIRM_VEC         (FLASH_ADDR_FIRM + FLASH_SIZE_TAG)

//   앱은 실행 중인 뱅크1 전체를 보호한다. 기록은 뱅크2 에만 한다 (w6300 과 같다).
#define FLASH_PROTECT_ADDR          0x08000000
#define FLASH_PROTECT_SIZE          (1024*1024)
```

부트로더는 보호 범위를 자기 자리(BOOT 128 KB)로 좁힌다. FIRM 을 써야 하기 때문이다.

---

## 4. 검증

뱅크2 (0x0810_0000) 에서 시험했다.

```
cli# flash info
sector size : 8 KB
bank size   : 1024 KB
BOOT    : 0x08000000 128 KB
FIRM    : 0x08020000 448 KB
PROTECT : 0x08000000 1024 KB

cli# flash erase 0x08100000 8192
erase OK : 2 ms
cli# flash write 0x08100000 0x12345678
write OK : 0 ms
cli# flash read 0x08100000 32
0x08100000 : 78 56 34 12 FF FF FF FF FF FF FF FF FF FF FF FF     <- 꼬리 12 B 는 0xFF
0x08100010 : FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF

cli# flash write 0x08100000 0xAABBCCDD
[E_] flashWrite() not blank 0x8100000                              <- 재기록 거부
cli# flash write 0x08000000 0x1
[E_] flashWrite() protected 0x8000000                              <- 뱅크1 보호
cli# flash erase 0x08000000 8192
[E_] flashErase() protected 0x8000000

cli# flash erase 0x08100000 458752
erase OK : 91 ms                                                   <- 448 KB (56 섹터)
```

| 측정 | 값 |
|---|---|
| 섹터 하나 지우기 | 2 ms |
| 448 KB (FIRM 크기) 지우기 | **91 ms** (섹터당 약 1.6 ms) |

N6 의 외부 NOR(앱 200 KB 지우기 0.95 s)보다 한 자릿수 빠르다. UART 다운로드에서 지우기 시간은 문제가 되지 않는다.

⚠️ 지운 섹터들은 이미 비어 있던 것이 대부분이다. 데이터가 꽉 찬 섹터를 지울 때도 같은지는 재지 않았다.

---

## 5. 부트로더에서 쓸 때 주의 (설계 메모)

- TAG(1 KB) 가 앱 첫 섹터(8 KB) 안에 있다. N6 처럼 TAG 만 지웠다 다시 쓸 수 없다.
  **ERASE 에서 TAG 자리까지 비우고, END 에서 지우지 않고 쓰기만 한다.** 빈 자리 검사가 그것을 보장한다
- 다운로드 패킷의 쓰기 오프셋은 16 B 배수여야 한다. 호스트 툴의 조각 크기를 16 의 배수로 둔다
- 부트로더는 뱅크1 에서 돌면서 뱅크1 의 FIRM 을 쓴다. 같은 뱅크라 쓰는 동안 코드 읽기가 멈춘다(RWW 가 아니다).
  그 사이 인터럽트 처리도 늦어진다 — UART 수신은 DMA 라 문제없을 것으로 보지만 실측으로 확인한다
