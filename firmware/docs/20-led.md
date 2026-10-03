# 20. LED

> 첫 기능. 목적은 LED 자체보다 **빌드 → 쓰기 → 실행 → 검증 루프를 확립**하는 것이다.
> 관련: [01-board.md](01-board.md), [11-project-skeleton.md](11-project-skeleton.md)
>
> **상태: 보드에서 실행 확인 (2026-10-03).** LED 극성은 눈으로 확인하는 것만 남았다.

---

## 1. 하드웨어

| 채널 | 부품 | MCU 핀 | 극성 |
|---|---|---|---|
| `_DEF_LED1` | D11 | **PC13** | active low ⚠️ |

회로도상 `+3.3V — D11 — R32 15K — PC13_LED` 로 읽힌다 → [01-board.md](01-board.md#3-led-시트-3).
참고 프로젝트의 PC13 LED 도 active low 다. 실물로 극성을 확인해야 한다.

---

## 2. 구현

### `src/hw/hw_def.h`

```c
#define _USE_HW_LED
#define      HW_LED_MAX_CH          1
```

### `src/hw/driver/led.c`

참고 프로젝트 것을 그대로 가져왔다. 핀도 같다.

```c
static led_tbl_t led_tbl[LED_MAX_CH] =
{
  {GPIOC, GPIO_PIN_13,  GPIO_PIN_RESET, GPIO_PIN_SET},
};
```

`ledInit()` 는 GPIOC 클럭을 켜고 push-pull output, `GPIO_SPEED_FREQ_LOW` 로 잡은 뒤 `ledOff()` 한다.
PC13 은 백업 도메인 핀이라 저속으로 써야 한다(데이터시트 제약) — LOW 그대로 두면 된다.

### `src/ap/ap.c`

```c
void apMain(void)
{
  uint32_t pre_time;


  pre_time = millis();
  while(1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }
  }
}
```

---

## 3. 클럭 (`bsp.c`)

참고 프로젝트의 `SystemClock_Config()` 를 그대로 쓴다. 보드 HSE 가 같은 25 MHz 다.

| | 값 |
|---|---|
| 전압 스케일 | VOS0 |
| PLL1 | HSE 25 MHz / M=2 → 12.5 MHz × N=40 → 500 MHz / P=2 → **250 MHz** |
| AHB / APB1 / APB2 / APB3 | /1 |
| Flash latency | 5 WS, programming delay 2 |
| 기타 | LSE ON (drive low), HSI48 ON (USB 용) |

`bspInit()` 은 이어서 ICACHE, DCACHE1, MPU 를 켠다. MPU 는 0x08FFF800 (UID 등 시스템 영역) 을
non-cacheable RO 로 잡는 참고 프로젝트 설정 그대로다.

---

## 4. 검증

### 빌드 ✅

```
FLASH:  8064 B / 2046 KB
$ arm-none-eabi-objdump -h build/stm32h5-fw.elf
  0 .isr_vector   0000024c  08000000
```

### 쓰기 ✅

`flash-stlink` 태스크와 같은 명령이다.

```
$ STM32_Programmer_CLI -c port=SWD mode=UR -w build/stm32h5-fw.bin 0x08000000 -v -rst
Erasing internal memory sectors [0 1]
Time elapsed during download operation: 00:00:00.179
Download verified successfully
MCU Reset
```

### 실행 — 리셋 없이 레지스터 읽기 ✅

`mode=HOTPLUG` 는 리셋하지 않고 붙는다. 약 0.5 s 간격으로 여섯 번 읽었다.

```
GPIOC_MODER(0x42020800)  GPIOC_ODR(0x42020814)  SystemCoreClock  uwTick
F7FFFFFF                 00000000               0EE6B280         000021FA
F7FFFFFF                 00002000               0EE6B280         0000241F
F7FFFFFF                 00000000               0EE6B280         0000261F
F7FFFFFF                 00002000               0EE6B280         00002823
```

| 값 | 해석 |
|---|---|
| `MODER = 0xF7FFFFFF` | bit[27:26] = `01` → PC13 output. 나머지는 리셋값(analog) |
| `ODR` bit13 | 읽을 때마다 `0 ↔ 1` → 토글 중 |
| `SystemCoreClock = 0x0EE6B280` | **250,000,000 Hz** |
| `uwTick` | 계속 증가 → SysTick 정상 |

### 토글 간격 — gdb ✅

gdbserver 로 붙어 `load` 한 뒤 `ledToggle` 에 브레이크를 걸고 세 번 잡았다 (`Debug FW` 와 같은 방식).

```
Breakpoint 2, ledToggle (ch=0 '\000') at src/hw/driver/led.c:62
SCC=250000000 tick=500 ODR=0x2000
tick=1000 ODR=0x0
tick=1500 ODR=0x2000
```

- 간격이 정확히 **500 ms**
- 토글 직전 ODR bit13 이 `1 → 0 → 1`

### 남은 것

- [ ] D11 이 눈으로 점멸하는지, `ledInit()` 직후 꺼져 있는지 (active low 확인)

---|---|
   | `SystemCoreClock` | 250000000 |
   | `GPIOC->MODER` bit[27:26] | `01` (PC13 output) |
   | `ledToggle` 브레이크 간격 (`millis()`) | 500 ms |
   | `GPIOC->ODR` bit13 | 토글 |

3. 극성: `ledOff()` 직후 꺼져 있으면 active low 가 맞다

---

## 5. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| (없음) 참고 프로젝트 startup 의 벡터 3칸이 비어 있음 | 파일이 H562 용 | H563 용 startup 으로 교체 → [11](11-project-skeleton.md#31-startup-파일이-h562-용이었다) |

---

## 6. 다음

- [x] 보드에서 LED 실측 (위 4절)
- [ ] UART (J1 U1_TXD/U1_RXD, PB14/PB15) + `logPrintf` + 부팅 배너
- [ ] CLI
