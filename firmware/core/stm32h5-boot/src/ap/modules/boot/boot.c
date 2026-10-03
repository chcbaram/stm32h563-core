#include "boot.h"
#include "util_core.h"
#include "module.h"


/* 앱(FW) 영역 레이아웃 (hw_def.h)

     FLASH_ADDR_FIRM       TAG    1 KB   firm_tag_t. 부트로더가 마지막에 쓴다 (커밋)
     FLASH_ADDR_FIRM_VEC   VECTOR 1 KB   앱 벡터 테이블
     + FLASH_SIZE_VEC      VER           firm_ver_t
                           이미지 나머지

   TAG 의 fw_crc 는 FLASH_ADDR_FIRM_VEC 부터 fw_size 바이트의 CRC-16(utilCalcCRC) 이다.
   stm32n6-boot / stm32h5-w6300 과 같은 형식이라 툴을 함께 쓴다.

   TAG 를 먼저 확인하고 본문을 읽는다. TAG 는 항상 마지막에 쓰므로 "TAG 유효 = 본문 기록 완료" 다.
   쓰다 만 영역(반쯤 프로그램된 쿼드워드)을 읽으면 ECC 오류로 NMI 가 날 수 있다 (w6300 docs/05). */
#define BOOT_CRC_BUF_SIZE     1024


#if CLI_USE(HW_BOOT)
static void cliBoot(cli_args_t *args);
#endif

static bool bootModuleInit(void);


MODULE_DEF(boot)
{
  .name     = "boot",
  .priority = MODULE_PRI_NORMAL,
  .init     = bootModuleInit,
};




bool bootModuleInit(void)
{
#if CLI_USE(HW_BOOT)
  cliAdd("boot", cliBoot);
#endif
  return true;
}

BootImgType_t bootVerifyFirm(void)
{
  firm_tag_t tag;
  firm_ver_t ver;
  uint8_t    buf[BOOT_CRC_BUF_SIZE];
  uint16_t   crc = 0;
  bool       has_ver;


  if (bootGetTag(&tag) != true)
  {
    // TAG 가 없으면 본문은 쓰다 만 것일 수 있다. VER 만 보고 끝낸다 (첫 섹터라 지우기 직후 0xFF 이거나 이미지다)
    return bootGetVer(&ver) ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }
  has_ver = bootGetVer(&ver);

  // 이미지가 신고한 크기와 다르면 옛 TAG 다 (weact 의 stale tag 규칙)
  if (has_ver == true && ver.firm_size > 0 && ver.firm_size != tag.fw_size)
  {
    return BOOT_IMG_VER;
  }

  for (uint32_t i = 0; i < tag.fw_size; i += sizeof(buf))
  {
    uint32_t n = tag.fw_size - i;

    if (n > sizeof(buf)) n = sizeof(buf);
    if (flashRead(FLASH_ADDR_FIRM_VEC + i, buf, n) != true)
    {
      return BOOT_IMG_NONE;
    }
    crc = utilCalcCRC(crc, buf, n);
  }

  if (crc != tag.fw_crc)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }
  return BOOT_IMG_TAG;
}

bool bootJumpFirm(void)
{
  void   (**jump_func)(void) = (void (**)(void))(FLASH_ADDR_FIRM_VEC + 4);
  uint32_t reset_handler;


  if (bootVerifyFirm() != BOOT_IMG_TAG)
  {
    logPrintf("[E_] bootJumpFirm() - no valid image\n");
    return false;
  }

  reset_handler = (uint32_t)*jump_func;
  if (reset_handler < FLASH_ADDR_FIRM_VEC || reset_handler >= FLASH_ADDR_FIRM + FLASH_SIZE_FIRM)
  {
    logPrintf("[E_] bootJumpFirm() - reset handler 0x%08X ?\n", (unsigned int)reset_handler);
    return false;
  }

  logPrintf("[  ] jump : 0x%08X\n", (unsigned int)reset_handler);
  logPrintf("\n");

  /*
   * 앱으로 넘기기 전 정리 (bspDeInit).
   *   UART : 원형 수신 DMA 를 멈추고 내린다. 앱이 같은 채널을 다시 설정한다
   *   SysTick / NVIC : 앱이 VTOR 를 옮기기 전에 남은 인터럽트가 뜨지 않게 끄고 대기도 지운다
   *
   * VTOR / MSP 는 건드리지 않는다 (w6300 과 같다).
   *   MSP  : 앱 Reset_Handler 의 ldr sp, =_estack
   *   VTOR : 앱 SystemInit() 의 SCB->VTOR = &_fw_flash_begin
   */
  resetSetBootMode(0);
  uartClose(HW_UART_CH_CLI);
  bspDeInit();

  (*jump_func)();

  return false;   // 도달하지 않는다
}

bool bootGetTag(firm_tag_t *p_tag)
{
  if (flashRead(FLASH_ADDR_FIRM, (uint8_t *)p_tag, sizeof(firm_tag_t)) != true)
    return false;

  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return false;

  if (p_tag->tag_crc != utilCalcCRC(0, (uint8_t *)p_tag, sizeof(firm_tag_t) - 4))
    return false;

  if (p_tag->fw_addr != FLASH_SIZE_TAG)
    return false;

  if (p_tag->fw_size == 0 || p_tag->fw_size > FLASH_SIZE_FIRM - FLASH_SIZE_TAG)
    return false;

  return true;
}

bool bootGetVer(firm_ver_t *p_ver)
{
  if (flashRead(FLASH_ADDR_FIRM_VEC + FLASH_SIZE_VEC, (uint8_t *)p_ver, sizeof(firm_ver_t)) != true)
    return false;

  return p_ver->magic_number == VERSION_MAGIC_NUMBER;
}


#if CLI_USE(HW_BOOT)
void cliBoot(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *img_str[] = {"NONE", "RAW", "VER", "TAG"};
    BootImgType_t img = bootVerifyFirm();
    firm_tag_t tag;
    firm_ver_t ver;

    cliPrintf("image : %s\n", img_str[img]);
    if (bootGetTag(&tag))
    {
      cliPrintf("tag   : %d bytes, crc 0x%04X\n", (int)tag.fw_size, (unsigned int)tag.fw_crc);
    }
    if (bootGetVer(&ver))
    {
      cliPrintf("ver   : %.*s %.*s, addr 0x%08X, size %d\n",
                (int)sizeof(ver.name_str), ver.name_str,
                (int)sizeof(ver.version_str), ver.version_str,
                (unsigned int)ver.firm_addr, (int)ver.firm_size);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "jump"))
  {
    if (bootJumpFirm() != true)
    {
      cliPrintf("jump fail\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot jump\n");
  }
}
#endif
