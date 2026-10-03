# 22. RTC + 리셋 원인

> 부트로더와 앱이 함께 쓰는 기반이다. 부트로더 요청(`resetToBoot`), 리셋 버튼 더블클릭, 리셋 원인 전달이
> 모두 RTC 백업 레지스터를 거친다. 부트로더보다 먼저 앱에서 만들고 검증했다.
> 관련: [21-uart-cli.md](21-uart-cli.md) (로그 시각)
>
> **상태: 보드에서 확인 (2026-10-03).** 리셋 버튼 더블클릭은 아직 눌러 보지 않았다.

---

## 1. 출처

| 파일 | 출처 | 이유 |
|---|---|---|
| `src/hw/driver/rtc.c`, `rtc.h` | N6 (`stm32n6-boot`) | H5 와 같은 RTC/TAMP IP. MSP 가 LSE 가 이미 켜져 있으면 다시 켜지 않고, RTCSEL 이 이미 LSE 면 백업 도메인을 리셋하지 않는다 |
| `src/hw/driver/reset.c`, `reset.h` | w6300 (`stm32h5-fw`) | H5 리셋 플래그 기준. 더블클릭 판정 순서가 H5 실기로 검증되어 있다 |

`reset.c` 는 `logPrintf` 를 `bsp.h` 에서 얻던 것을 `#include "log.h"` 로 바꿨다 (이 프로젝트의 `bsp.h` 는 선언하지 않는다).

---

## 2. 클럭 / 백업 레지스터

- RTC 클럭은 보드의 32.768 kHz LSE (Y3). `SystemClock_Config()` 이 이미 LSE 를 켜고, RTC MSP 가 RTCSEL 을 LSE 로 둔다
- 백업 레지스터는 TAMP 블록(`TAMP_BKPxR`)에 있다. **리셋에는 남고 전원(VBAT 포함)이 끊기면 지워진다.** 보드에는 VBAT 커넥터 J6 가 있다

배정은 w6300 과 같다. 부트로더를 붙이면 부트로더 `hw_def.h` 도 이 표와 같아야 한다.

| 레지스터 | 이름 | 용도 |
|---|---|---|
| `RTC_BKP_DR3` | `HW_RTC_BOOT_MODE` | 부트 요청 플래그 (`MODE_BIT_BOOT`, `MODE_BIT_UPDATE`) |
| `RTC_BKP_DR4` | `HW_RTC_RESET_BITS` | 리셋 원인 (부트로더가 읽고 지운 것을 앱에 넘긴다) |
| `RTC_BKP_DR5` | `HW_RTC_RESET_CNT` | 리셋 버튼 클릭 수 (매직 `0xA55A` + 값. 매직은 `reset.c` 안에 둔다) |
| `RTC_BKP_DR6` | `HW_RTC_BOOT_TRY` | 부팅 확인 카운터 (슬롯 롤백 단계에서 쓴다) |
| `RTC_BKP_DR7` | `HW_RTC_FAULT_CNT` | 폴트 횟수 (fault 단계에서 쓴다) |
| `RTC_BKP_DR8` | `HW_RTC_ECC_ADDR` | ECC 오류 주소 |

### 시각을 맞췄는지 — `rtcIsTimeSet()` 추가

N6 `log.c` 는 RTC 가 있으면 줄 머리에 RTC 시각을 찍는다. 그런데 `rtcGetTime()` 은 시각을 맞춘 적이 없어도
true 를 돌려줘서, 그대로 두면 로그가 `[00:37:04]` 같은 의미 없는 값이 된다 (N6 도 같은 구현이다).

**`RTC_ICSR.INITS`** 로 가른다. 달력의 연도가 0 이 아니면 하드웨어가 켜는 비트이고 백업 도메인에 있어 리셋 뒤에도 남는다.

```c
bool rtcIsTimeSet(void)
{
  if (is_init != true)
    return false;

  return (RTC->ICSR & RTC_ICSR_INITS) ? true : false;
}
```

`log.c` 의 줄 머리는 `rtcIsTimeSet() && rtcGetTime()` 일 때만 RTC 시각, 아니면 부팅 후 경과 시간이다.

---

## 3. 리셋 원인과 더블클릭 판정

`HW_RESET_BOOT = 1` 이면 `resetInit()` 이 RCC 리셋 플래그를 직접 읽고 지운 뒤 백업 레지스터에 남긴다.
이 문서를 쓸 때는 부트로더가 없어 앱이 1 이었다. **지금은 부트로더가 1, 앱이 0** 이다 ([26](26-bootloader.md)) — 앱은 부트로더가 남긴 값을 읽는다
(앱이 읽을 때는 플래그가 이미 지워져 있기 때문이다).

| 리셋 | RCC 플래그 | 더블클릭 카운트 |
|---|---|---|
| 전원 인가 | BOR + PIN | 0 (항상 새 시작) |
| `NVIC_SystemReset()` / 프로그래머 리셋 | **SOFT + PIN** | 0 |
| 워치독 | WDG (+ PIN) | 0 |
| 리셋 버튼만 | PIN | +1 |

H5 는 소프트 리셋도 NRST 핀으로 전파되어 PINRSTF 가 같이 선다 (아래 4절에서 확인). 그래서 **BOR → SOFT/WDG → PIN 순서**로 걸러야
`resetToBoot()` 이나 전원을 두 번 껐다 켠 것이 더블클릭으로 잘못 잡히지 않는다.

카운트가 1 이면 LED 를 켠 채 300 ms 기다리며 두 번째 클릭을 받는다. 그 안에 다시 누르면 다음 부팅에서 카운트가 2 → 부트로더에 머문다.
전원 인가 부팅에서는 이 지연이 없다.

---

## 4. 검증

### 부팅 로그

```
[OK] rtcInit()
[OK] resetInit()
     RESET_BIT_PIN
     RESET_BIT_SOFT
     reset_count : 0
```

SWD 로 쓰고 `-rst` 로 리셋한 직후다. 프로그래머의 리셋이 **SOFT + PIN** 으로 잡힌다 — 위 표 그대로다.

### 백업 레지스터와 시각이 리셋에 남는다

```
cli# rtc info
LSE     : ready
Date    : 2000-01-01 00:37:04       <- 시각을 맞춘 적 없음 (INITS = 0). LSE 는 계속 돌고 있었다

cli# rtc reg 10 0x12345678
cli# rtc set date 26 10 3
cli# rtc set time 21 50 0
cli# reset reset                    <- NVIC_SystemReset()

cli# rtc reg 10
BKP10 : 0x12345678                  <- 남았다
cli# rtc get info
Y:26 M:10 D:03, H:21 M:50 S:02      <- 시각도 남았다
```

### 로그 줄 머리가 RTC 시각으로 바뀐다

```
cli# log list
[    0.002]
[ Firmware Begin... ]
[    0.004]	Booting..Name 		: STM32H5-FW
...
[21:50:00]	[OK] rtcInit()              <- rtcInit() 이후, 시각을 맞춘 뒤라 RTC 시각
[21:50:00]	[OK] resetInit()
```

### 남은 것

- [ ] 리셋 버튼(S2) 더블클릭 → `reset_count : 2`
- [ ] 전원을 두 번 껐다 켜도 카운트가 오르지 않는지
- [ ] VBAT 없이 전원을 끊으면 시각/백업 레지스터가 지워지는지 (지워지는 게 정상)

---

## 5. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `reset.c` 에서 `logPrintf` 암시적 선언 | w6300 `bsp.h` 는 `logPrintf` 를 선언했지만 이 프로젝트 `bsp.h` 는 안 한다 | `#include "log.h"` |
| (설계) RTC 를 켜면 로그 시각이 `[00:xx:xx]` 가 된다 | `rtcGetTime()` 이 미설정 상태에서도 true | `rtcIsTimeSet()` (`INITS`) 로 가른다 |
