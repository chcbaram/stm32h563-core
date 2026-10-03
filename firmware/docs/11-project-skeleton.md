# 11. core/stm32h5-fw 프로젝트 골격

> `firmware/core/stm32h5-fw` — STM32H563_CORE 용 펌웨어
> 참고: `~/hdd/git/stm32h5-w6300/firmware/stm32h5-fw` (같은 STM32H563, 2026-08-22 최신)

---

## 1. 전제 — 부트로더 없이 시작한다

참고 프로젝트는 부트로더(`stm32h5-boot`, 0x08000000 128 KB) 뒤에 앱이 놓이는 구조다.
여기서는 아직 부트로더가 없으므로 **앱이 0x08000000 에서 바로 시작**한다.
부트로더를 붙이게 되면 링커 스크립트와 `system_stm32h5xx.c` 의 VTOR 만 옮기면 되도록
`_fw_flash_begin` / `.version` 구조는 그대로 두었다.

최종 구조는 참고 프로젝트처럼 `core/stm32h5-boot` + `core/stm32h5-fw` 이고,
확장보드 펌웨어도 같은 부트로더 뒤에 놓인다 → [README 폴더 구성](README.md#폴더-구성).

출력 파일 이름은 폴더 이름과 같은 `PRJ_NAME = stm32h5-fw` 다 → `build/stm32h5-fw.{elf,bin}`.
`.version` 영역의 `_DEF_BOARD_NAME` 도 폴더 이름에 맞춰 `"STM32H5-FW"` 다.
확장보드 펌웨어와 이름이 같아지므로, 어느 보드 것인지 구분하는 방법은 확장보드를 붙일 때 정한다.

---

## 2. 디렉터리 구조

```
firmware/core/stm32h5-fw/
├── CMakeLists.txt
├── .clang-format                 참고 프로젝트에서 그대로
├── .vscode/
│   ├── tasks.json                build / flash 태스크 (macOS + Windows)
│   ├── launch.json               cortex-debug (CubeCLT, 버전 없는 경로)
│   └── c_cpp_properties.json
├── prj/stm32h5-fw.code-workspace   이 프로젝트 폴더를 연다
├── tools/
│   └── arm-none-eabi-gcc.cmake   툴체인 정의 (N6 프로젝트 것)
└── src/
    ├── main.c / main.h
    ├── ap/                       애플리케이션 로직
    │   └── ap.c / ap.h / ap_def.h
    ├── bsp/                      보드/칩 초기화
    │   ├── bsp.c / bsp.h         클럭, 캐시, MPU, delay, Error_Handler
    │   ├── device/               ST 제공 파일 (hal_conf, it, system, msp, syscalls)
    │   ├── ldscript/             STM32H563xx_FLASH.ld
    │   └── startup/              startup_stm32h563xx.s
    ├── common/                   칩 비의존 공통 코드
    │   ├── def.h / err_code.h / evt_code.h
    │   └── hw/include/           드라이버 공개 헤더 (led.h)
    ├── hw/                       드라이버 계층
    │   ├── hw.c / hw.h / hw_def.h
    │   └── driver/               led.c
    └── lib/ST/                   벤더 소스 (참고 프로젝트에서 복사)
        ├── CMSIS/Include
        ├── CMSIS/Device/ST/STM32H5xx/Include
        └── STM32H5xx_HAL_Driver/{Inc,Src}
```

호출 흐름은 참고 프로젝트와 같다. RTOS 분기는 뺐다.

```
main() -> bspInit()   HAL, 클럭, I/D 캐시, MPU
       -> hwInit()    드라이버 초기화 (led)
       -> apInit()
       -> apMain()    무한 루프
```

`hw_def.h` 의 `_USE_HW_xxx` / `HW_xxx_MAX_CH` 로 드라이버를 켜고 끄는 방식도 그대로다.

---

## 3. 참고 프로젝트에서 바꾼 것

| 항목 | 참고 프로젝트 | 여기 | 이유 |
|---|---|---|---|
| 시작 주소 | 0x08020400 (부트로더 뒤) | **0x08000000** | 부트로더 없음 |
| startup | `startup_stm32h562xx.s` | **`startup_stm32h563xx.s`** (참고 저장소의 `stm32h5-cube` 에서) | 아래 3.1 |
| 폴트 핸들러 | asm → `faultReset()` 로 원인 기록 | 기본 무한루프 | fault 드라이버를 아직 안 넣었다 |
| `_write` (printf) | `uartWrite()` | 버림 | UART 를 아직 안 넣었다 |
| SysTick | `HAL_IncTick()` + `swtimerISR()` | `HAL_IncTick()` 만 | swtimer 를 아직 안 넣었다 |
| `delay()` | 기다리는 동안 `cliLoopIdle()` | `HAL_Delay()` | CLI 를 아직 안 넣었다 |
| HAL 모듈 | + XSPI, RTC, UART, PCD | GPIO, EXTI, DMA, RCC, FLASH, PWR, CORTEX, ICACHE, DCACHE | 쓰는 것만 |
| 툴체인 cmake | 원본 | N6 프로젝트 것 | [10](10-dev-environment.md) 2절 |

### 3.1 startup 파일이 H562 용이었다

참고 프로젝트의 `src/bsp/startup/startup_stm32h562xx.s` 는 파일 안 주석이 `startup_stm32h573xx.s` 이고,
벡터 테이블을 H563 용과 비교하면 세 칸이 비어 있다.

```
$ diff <(벡터 목록 h562) <(벡터 목록 h563)
124c124
< 0
> SDMMC2_IRQHandler
128,129c128,129
< 0
< 0
> ETH_IRQHandler
> ETH_WKUP_IRQHandler
```

H563RIT6 에는 ETH 와 SDMMC2 가 있다. 지금은 쓰지 않아 동작에 차이는 없지만, 나중에 붙일 때
핸들러가 불리지 않는 문제가 되므로 처음부터 H563 용을 쓴다.
이 startup 은 `HardFault_Handler` 등을 `Default_Handler` 의 weak 별칭으로 두므로
`stm32h5xx_it.c` 에서 일반 C 핸들러로 정의했다.

---

## 4. 링커 스크립트

```
MEMORY
{
  NO_INIT   (xrw) : ORIGIN = 0x20000000, LENGTH = 8K
  RAM       (xrw) : ORIGIN = 0x20002000, LENGTH = 640K-8K

  VECTOR     (rx) : ORIGIN = 0x08000000, LENGTH = 1K
  VER        (rx) : ORIGIN = 0x08000400, LENGTH = 1K
  FLASH      (rx) : ORIGIN = 0x08000800, LENGTH = 2048K-2K
}
```

| 영역 | 내용 |
|---|---|
| VECTOR | 벡터 테이블. `_fw_flash_begin` 이 여기 시작이고 `SystemInit()` 이 `SCB->VTOR` 에 넣는다 |
| VER | `firm_ver_t` (매직 `"VER "`, 버전 문자열, 보드 이름, 시작 주소). 나중에 부트로더/툴이 읽는다 |
| NO_INIT | 리셋해도 지워지지 않는 영역 (참고 프로젝트 구조 유지) |

빌드 결과 확인:

```
$ arm-none-eabi-objdump -h build/stm32h5-fw.elf
  0 .isr_vector   0000024c  08000000
  1 .version      00000048  08000400
  2 .text         00001f50  08000800

$ xxd -l 8 build/stm32h5-fw.bin
00000000: 0000 0a20 ed0c 0008      MSP=0x200A0000  Reset=0x08000CED
```

MSP `0x200A0000` 은 SRAM 끝 (0x20000000 + 640 KB) 이다.

---

## 5. CMake

참고 프로젝트 `CMakeLists.txt` 에서 RTOS, USB, UF2 생성, 버전 파싱을 빼고 그대로 두었다.

```cmake
-mcpu=cortex-m33 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
-DSTM32H563xx -DUSE_HAL_DRIVER
```

HAL 은 `Src/*.c` 를 통째로 glob 한다. 비활성 모듈 소스는 `#ifdef HAL_xxx_MODULE_ENABLED` 로 비어서
H5 HAL 에서는 문제가 없다 (N6 는 `ll_dlyb.c` 때문에 명시 목록이 필요했다).

---

## 6. 빌드

```bash
cd firmware/core/stm32h5-fw
cmake -S . -B build; cmake --build build -j20
```

```
Memory region     Used Size  Region Size  %age Used
           RAM:       1632 B       632 KB      0.25%
           VER:         72 B         1 KB      7.03%
         FLASH:       8064 B      2046 KB      0.38%

   text    data     bss     dec
   8700      24    1616   10340
```

경고 없음.

---

## 7. 의도적으로 넣지 않은 것

참고 프로젝트의 UART, 로그, CLI, fault, RTC, reset, flash, USB(CDC/HID), cmd, WIZnet, 부트로더 연동은
전부 뺐다. 기능 하나씩 붙이면서 `20~` 번 문서로 남긴다.
