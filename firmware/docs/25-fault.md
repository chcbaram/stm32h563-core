# 25. 폴트 기록

> 폴트가 나면 레지스터와 원인을 남기고 리셋한다. 다음 부팅 로그에 찍힌다.
> 이전에는 무한루프라 보드가 멈추고 원인이 남지 않았다 ([24](24-qspi.md) 6절에서 실제로 겪었다).
> 관련: [22-rtc-reset.md](22-rtc-reset.md) (폴트 횟수 백업 레지스터)
>
> **상태: 보드에서 확인 (2026-10-03).** 버스 오류 / 정의되지 않은 명령 / 0 나누기.

---

## 1. 구성

| 파일 | 내용 |
|---|---|
| `src/bsp/device/stm32h5xx_it.c` | `HardFault / MemManage / BusFault / UsageFault_Handler` 를 naked 함수로. 스택 프레임 주소를 `*_Handler_C(p_stack)` 로 넘긴다 |
| `src/hw/driver/fault.c`, `fault.h` | w6300 것을 바탕으로. 레지스터를 `.noinit` 에 적고 `NVIC_SystemReset()`. 다음 부팅의 `faultInit()` 이 찍는다 |

### 진입부 — startup 은 건드리지 않는다

w6300 은 startup 어셈블리 파일에 진입부를 두었다. 여기서는 큐브의 `startup_stm32h563xx.s` 를 그대로 두고
(핸들러가 weak 별칭이다) `stm32h5xx_it.c` 에 naked 함수로 넣었다.

```c
__attribute__((naked)) void HardFault_Handler(void)
{
  // 예외가 스택 프레임을 쌓은 쪽(EXC_RETURN bit2: 0 = MSP, 1 = PSP)을 R0 로 넘긴다
  __asm volatile
  (
    "tst   lr, #4             \n"
    "ite   eq                 \n"
    "mrseq r0, msp            \n"
    "mrsne r0, psp            \n"
    "b     HardFault_Handler_C   \n"
  );
}
```

`p_stack[0..7]` = R0 R1 R2 R3 R12 LR **PC** xPSR. PC 는 폴트를 낸 명령이다.

### w6300 에서 바꾼 것

| | w6300 | 여기 |
|---|---|---|
| 원인 레지스터 | 없음 (PC 만) | **CFSR / HFSR / MMFAR / BFAR** 를 남기고 자주 보는 비트를 풀어 쓴다 |
| SRAM 에서 난 폴트 | "TODO: 일단 무시" — 기록을 지웠다 | 그대로 기록 |
| `faultGetLog()` | 매번 `.noinit` 원본을 복사 | 폴트 부팅일 때 걷어 둔 사본 |
| CLI | 없음 | `fault info`, `fault test bus|udf|div` |

MemManage / BusFault / UsageFault 는 SHCSR 에서 따로 켜지 않아 지금은 모두 HardFault 로 올라간다 (`HFSR FORCED`).
원인은 CFSR 로 구분되므로 따로 켤 필요는 없다.

### 남는 것

| 어디 | 무엇 | 언제 지워지나 |
|---|---|---|
| `.noinit` (SRAM 0x2000_0000, 링커 `NO_INIT`) | 레지스터 전부 | 다음 부팅의 `faultInit()` 이 매직을 지운다. 전원이 끊기면 쓰레기값 (매직으로 거른다) |
| RTC 백업 `HW_RTC_FAULT_CNT` | 폴트 횟수 | `resetConfirmBoot()`. 슬롯 롤백 단계에서 "부팅 직후 계속 죽는 앱" 판정에 쓴다 |

---

## 2. 검증

`fault test` 로 일부러 낸다. 리셋된 뒤 부팅 로그:

```
[!!] Fault Boot
     Msg   : HardFault
     PC    : 0x0800341C
     LR    : 0x08002DC1
     ...
     CFSR  : 0x00010000
     HFSR  : 0x40000000 (FORCED)
       USG : undefined instruction
[OK] faultInit()
```

| 시험 | CFSR | 풀어 쓴 원인 | PC → 소스 (`addr2line`) |
|---|---|---|---|
| `fault test udf` | `0x00010000` | undefined instruction | `fault.c:176` (`udf #0`) |
| `fault test div` | `0x02000000` | divide by zero | `fault.c:183` (`a / b`) |
| `fault test bus` | `0x00008200` | precise data bus error, **BFAR 0x9F000000** | |
| `md 0x91000000 1` (XIP 꺼짐) | `0x00008200` | precise data bus error, **BFAR 0x91000000** | `cli.c:822` (`cliMemoryDump`) |

마지막이 [24](24-qspi.md) 에서 보드를 멈추게 했던 경우다. 이제는 어느 주소를 읽다 났는지까지 남는다.

```bash
arm-none-eabi-addr2line -f -p -e build/stm32h5-fw.elf 0x0800341C
# cliFault at .../src/hw/driver/fault.c:176
```

정상 리셋(`reset reset`) 뒤에는 `fault boot : no` 로 돌아가고, 횟수는 백업 레지스터에 누적된다 (`fault count : 5`).

---

## 3. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `fault test bus` 가 폴트를 내지 않음 (0x6000_0000 읽기) | FMC 를 켜지 않은 FMC 영역은 버스 오류가 나지 않았다 | OCTOSPI 창의 칩 밖 주소 `0x9F00_0000`. XIP 가 켜져 있든 꺼져 있든 버스 오류 |
