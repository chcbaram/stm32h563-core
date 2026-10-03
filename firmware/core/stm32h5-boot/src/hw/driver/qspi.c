#include "qspi.h"



#ifdef _USE_HW_QSPI
#include "qspi/w25q256jv.h"
#include "cli.h"
#include "log.h"


/* W25Q256JV (32 MB) on OCTOSPI1, quad SPI.

   32 MB 라 16 MB 를 넘는 주소는 4 바이트 주소가 필요하다. 칩의 주소 모드(ADS, 0xB7/0xE9)에
   기대지 않고 **4 바이트 주소 전용 명령**만 쓴다. 칩이 3 바이트 모드로 켜지든(기본값, ADP=0)
   리셋 명령(0x99)으로 돌아가든 같은 명령이 그대로 통한다.

     읽기          0xEC  Fast Read Quad I/O (4B)   주소 4 선, 모드 바이트 + 더미 4 클럭
     페이지 쓰기   0x34  Quad Input Page Program (4B)
     4 KB 지우기   0x21  Sector Erase (4B)
     64 KB 지우기  0xDC  Block Erase (4B)

   상태는 Winbond 의 SR1 (0x05) 로 본다. 참고한 H7R 드라이버의 0x70 (Flag Status) 은 Micron 명령이다. */
#define QSPI_CMD_READ_QUAD_IO_4B      0xEC
#define QSPI_CMD_PROG_QUAD_4B         0x34
#define QSPI_CMD_ERASE_4K_4B          0x21
#define QSPI_CMD_ERASE_64K_4B         0xDC

#define QSPI_XIP_ADDR                 OCTOSPI1_BASE   // 0x9000_0000, 메모리 맵 창

#define QSPI_JEDEC_WINBOND            0xEF
#define QSPI_JEDEC_TYPE               0x40
#define QSPI_JEDEC_CAP_32MB           0x19     // 2^25 = 32 MB

#define QSPI_SR2_QE                   (1<<1)   // Quad Enable

#define QSPI_OK            ((uint8_t)0x00)
#define QSPI_ERROR         ((uint8_t)0x01)
#define QSPI_BUSY          ((uint8_t)0x02)
#define QSPI_NOT_SUPPORTED ((uint8_t)0x04)


static bool is_init = false;
static XSPI_HandleTypeDef hqspi;


static uint8_t qspiHwInit(void);
static uint8_t qspiHwRead(uint8_t* p_data, uint32_t addr, uint32_t length);
static uint8_t qspiHwWrite(uint8_t* p_data, uint32_t addr, uint32_t length);
static uint8_t qspiHwErase(uint8_t cmd, uint32_t addr, uint32_t timeout);
static uint8_t qspiHwEraseChip(void);
static uint8_t qspiHwGetID(uint8_t *p_id, uint32_t length);
static uint8_t qspiHwConfig(void);
static uint8_t qspiHwEnableMemoryMappedMode(void);

static void    qspiCmdInit(XSPI_RegularCmdTypeDef *p_cmd, uint8_t instruction);
static uint8_t qspiResetMemory(void);
static uint8_t qspiWriteEnable(void);
static uint8_t qspiAutoPollingMemReady(uint32_t timeout);
static uint8_t qspiReadStatus(uint8_t cmd, uint8_t *p_data);
static uint8_t qspiWriteStatus(uint8_t cmd, uint8_t data);
static bool    qspiXipSuspend(void);
static void    qspiXipResume(bool was_xip);

#if CLI_USE(HW_QSPI)
static void cliCmd(cli_args_t *args);
#endif






bool qspiInit(void)
{
  bool    ret = false;
  uint8_t id[3] = {0, 0, 0};


  if (qspiHwInit() == QSPI_OK && qspiHwGetID(id, 3) == QSPI_OK)
  {
    if (id[0] == QSPI_JEDEC_WINBOND && id[1] == QSPI_JEDEC_TYPE && id[2] == QSPI_JEDEC_CAP_32MB)
    {
      ret = true;
    }
  }

  if (ret == true)
  {
    logPrintf("[OK] qspiInit()\n");
    logPrintf("     W25Q256JV Found\n");
    logPrintf("     CLK  : %d Mhz\n", (int)(HAL_RCC_GetHCLKFreq() / (hqspi.Init.ClockPrescaler + 1) / 1000000));
    logPrintf("     ADDR : 0x%X\n", QSPI_XIP_ADDR);
    logPrintf("     SIZE : %d MB\n", (int)(qspiGetLength()/1024/1024));
  }
  else
  {
    logPrintf("[E_] qspiInit()\n");
    logPrintf("     W25Q256JV Not Found %X %X %X\n", id[0], id[1], id[2]);
  }

  is_init = ret;

#if CLI_USE(HW_QSPI)
  cliAdd("qspi", cliCmd);
#endif
  return ret;
}

bool qspiIsInit(void)
{
  return is_init;
}

bool qspiReset(void)
{
  if (is_init != true)
    return false;

  if (qspiGetXipMode() == true && HAL_XSPI_Abort(&hqspi) != HAL_OK)
    return false;

  return qspiResetMemory() == QSPI_OK;
}

bool qspiRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (is_init != true || length == 0)
    return false;
  if (addr >= qspiGetLength() || length > qspiGetLength() - addr)
    return false;

  if (qspiGetXipMode() == true)
  {
    memcpy(p_data, (void *)(QSPI_XIP_ADDR + addr), length);
    return true;
  }

  return qspiHwRead(p_data, addr, length) == QSPI_OK;
}

bool qspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool ret;
  bool was_xip;


  if (is_init != true || length == 0)
    return false;
  if (addr >= qspiGetLength() || length > qspiGetLength() - addr)
    return false;

  was_xip = qspiXipSuspend();
  ret     = qspiHwWrite(p_data, addr, length) == QSPI_OK;
  qspiXipResume(was_xip);

  return ret;
}

bool qspiEraseBlock(uint32_t block_addr)
{
  bool ret;
  bool was_xip;


  if (is_init != true || block_addr >= qspiGetLength())
    return false;

  was_xip = qspiXipSuspend();
  ret     = qspiHwErase(QSPI_CMD_ERASE_64K_4B, block_addr, W25Q256JV_SECTOR_ERASE_MAX_TIME) == QSPI_OK;
  qspiXipResume(was_xip);

  return ret;
}

bool qspiEraseSector(uint32_t sector_addr)
{
  bool ret;
  bool was_xip;


  if (is_init != true || sector_addr >= qspiGetLength())
    return false;

  was_xip = qspiXipSuspend();
  ret     = qspiHwErase(QSPI_CMD_ERASE_4K_4B, sector_addr, W25Q256JV_SUBSECTOR_ERASE_MAX_TIME) == QSPI_OK;
  qspiXipResume(was_xip);

  return ret;
}

// 4 KB 경계로 넓혀 지운다. 64 KB 로 정렬된 구간은 64 KB 명령으로 지운다 (4 KB 16 번보다 빠르다).
bool qspiErase(uint32_t addr, uint32_t length)
{
  bool     ret = true;
  uint32_t begin;
  uint32_t end;


  if (is_init != true || length == 0)
    return false;
  if (addr >= qspiGetLength() || length > qspiGetLength() - addr)
    return false;

  begin = addr & ~(W25Q256JV_SUBSECTOR_SIZE - 1);
  end   = (addr + length + W25Q256JV_SUBSECTOR_SIZE - 1) & ~(W25Q256JV_SUBSECTOR_SIZE - 1);

  while (begin < end && ret == true)
  {
    if ((begin % W25Q256JV_SECTOR_SIZE) == 0 && (end - begin) >= W25Q256JV_SECTOR_SIZE)
    {
      ret    = qspiEraseBlock(begin);
      begin += W25Q256JV_SECTOR_SIZE;
    }
    else
    {
      ret    = qspiEraseSector(begin);
      begin += W25Q256JV_SUBSECTOR_SIZE;
    }
  }

  return ret;
}

bool qspiEraseChip(void)
{
  bool ret;
  bool was_xip;


  if (is_init != true)
    return false;

  was_xip = qspiXipSuspend();
  ret     = qspiHwEraseChip() == QSPI_OK;
  qspiXipResume(was_xip);

  return ret;
}

bool qspiGetStatus(void)
{
  uint8_t reg = 0;

  if (is_init != true || qspiGetXipMode() == true)
    return false;
  if (qspiReadStatus(READ_STATUS_REG_CMD, &reg) != QSPI_OK)
    return false;

  return (reg & W25Q256JV_SR_WIP) == 0;
}

bool qspiGetInfo(qspi_info_t* p_info)
{
  memset(p_info, 0, sizeof(qspi_info_t));
  p_info->FlashSize          = W25Q256JV_FLASH_SIZE;
  p_info->EraseSectorSize    = W25Q256JV_SUBSECTOR_SIZE;
  p_info->EraseSectorsNumber = W25Q256JV_FLASH_SIZE / W25Q256JV_SUBSECTOR_SIZE;
  p_info->ProgPageSize       = W25Q256JV_PAGE_SIZE;
  p_info->ProgPagesNumber    = W25Q256JV_FLASH_SIZE / W25Q256JV_PAGE_SIZE;

  if (is_init == true && qspiGetXipMode() == false)
  {
    qspiHwGetID(p_info->device_id, 3);
  }
  return true;
}

bool qspiEnableMemoryMappedMode(void)
{
  if (is_init != true)
    return false;

  return qspiHwEnableMemoryMappedMode() == QSPI_OK;
}

bool qspiSetXipMode(bool enable)
{
  if (is_init != true)
    return false;

  if (enable == qspiGetXipMode())
    return true;

  if (enable)
    return qspiEnableMemoryMappedMode();

  return HAL_XSPI_Abort(&hqspi) == HAL_OK;
}

bool qspiGetXipMode(void)
{
  return HAL_XSPI_GetState(&hqspi) == HAL_XSPI_STATE_BUSY_MEM_MAPPED;
}

uint32_t qspiGetAddr(void)
{
  return QSPI_XIP_ADDR;
}

uint32_t qspiGetLength(void)
{
  return W25Q256JV_FLASH_SIZE;
}

// 메모리 맵 중에는 명령을 보낼 수 없다. 잠깐 풀고 끝나면 되돌린다.
bool qspiXipSuspend(void)
{
  if (qspiGetXipMode() != true)
    return false;

  HAL_XSPI_Abort(&hqspi);
  return true;
}

// 쓰기 / 지우기 뒤에 부른다.
//
// DCACHE1 은 외부 메모리(OCTOSPI 메모리 맵 0x9000_0000~) 를 캐시한다. 메모리 맵으로 읽어 캐시에 올라간 줄은
// 명령으로 고쳐 써도 그대로 남아, 다시 읽으면 옛 값이 나온다 (실측: 0x9100_0044 에 쓴 뒤에도 0xFF).
// 지금 XIP 가 아니어도 이전 XIP 때 올라간 줄이 남아 있을 수 있어 항상 비운다.
void qspiXipResume(bool was_xip)
{
  if (READ_BIT(DCACHE1->CR, DCACHE_CR_EN) != 0U)
  {
    SET_BIT(DCACHE1->CR, DCACHE_CR_CACHEINV);
    while (READ_BIT(DCACHE1->SR, DCACHE_SR_BUSYF) != 0U)
    {
    }
  }

  if (was_xip == true)
  {
    qspiHwEnableMemoryMappedMode();
  }
}





uint8_t qspiHwInit(void)
{
  hqspi.Instance = OCTOSPI1;
  if (HAL_XSPI_DeInit(&hqspi) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* 커널 클럭은 HCLK 250 MHz (HAL_XSPI_MspInit).
     ClockPrescaler = 2 -> 250 / 3 = 83 MHz. W25Q256JV 의 Quad I/O 읽기는 133 MHz 까지지만
     보드는 라인마다 27Ω 직렬 저항이 있다. 여유를 두고 시작한다.

     MemorySize 는 주소 비트 수다. HAL_XSPI_SIZE_256MB = 256 Mbit = 32 MB (2^25). */
  hqspi.Init.FifoThresholdByte       = 4;
  hqspi.Init.MemoryMode              = HAL_XSPI_SINGLE_MEM;
  hqspi.Init.MemoryType              = HAL_XSPI_MEMTYPE_MICRON;
  hqspi.Init.MemorySize              = HAL_XSPI_SIZE_256MB;
  hqspi.Init.ChipSelectHighTimeCycle = 2;                       // tSHSL 50 ns 이상 (12 ns x 2 + 지연)
  hqspi.Init.FreeRunningClock        = HAL_XSPI_FREERUNCLK_DISABLE;
  hqspi.Init.ClockMode               = HAL_XSPI_CLOCK_MODE_0;
  hqspi.Init.WrapSize                = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hqspi.Init.ClockPrescaler          = 2;
  hqspi.Init.SampleShifting          = HAL_XSPI_SAMPLE_SHIFT_HALFCYCLE;
  hqspi.Init.DelayHoldQuarterCycle   = HAL_XSPI_DHQC_DISABLE;
  hqspi.Init.ChipSelectBoundary      = HAL_XSPI_BONDARYOF_NONE;
  hqspi.Init.DelayBlockBypass        = HAL_XSPI_DELAY_BLOCK_BYPASS;
  hqspi.Init.Refresh                 = 0;
  if (HAL_XSPI_Init(&hqspi) != HAL_OK)
  {
    logPrintf("[E_] HAL_XSPI_Init()\n");
    return QSPI_ERROR;
  }

  if (qspiResetMemory() != QSPI_OK)
  {
    logPrintf("[E_] qspiResetMemory()\n");
    return QSPI_NOT_SUPPORTED;
  }

  if (qspiHwConfig() != QSPI_OK)
  {
    logPrintf("[E_] qspiHwConfig()\n");
    return QSPI_NOT_SUPPORTED;
  }

  return QSPI_OK;
}

// Quad 명령을 쓰려면 SR2 의 QE 가 1 이어야 한다. W25Q256JV-IQ 는 출하 때 1 이지만 확인하고 켠다.
uint8_t qspiHwConfig(void)
{
  uint8_t reg = 0;


  if (qspiReadStatus(READ_STATUS_REG2_CMD, &reg) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  if ((reg & QSPI_SR2_QE) == 0)
  {
    if (qspiWriteStatus(WRITE_STATUS_REG2_CMD, reg | QSPI_SR2_QE) != QSPI_OK)
    {
      return QSPI_ERROR;
    }
  }

  return QSPI_OK;
}

uint8_t qspiHwRead(uint8_t* p_data, uint32_t addr, uint32_t length)
{
  XSPI_RegularCmdTypeDef s_command;


  qspiCmdInit(&s_command, QSPI_CMD_READ_QUAD_IO_4B);
  s_command.AddressMode         = HAL_XSPI_ADDRESS_4_LINES;
  s_command.AddressWidth        = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address             = addr;
  s_command.AlternateBytesMode  = HAL_XSPI_ALT_BYTES_4_LINES;   // 모드 바이트 M7-0 = 0 (연속 읽기 안 함)
  s_command.AlternateBytesWidth = HAL_XSPI_ALT_BYTES_8_BITS;
  s_command.AlternateBytes      = 0;
  s_command.DataMode            = HAL_XSPI_DATA_4_LINES;
  s_command.DataLength          = length;
  s_command.DummyCycles         = W25Q256JV_DUMMY_CYCLES_READ_QUAD;

  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Receive(&hqspi, p_data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiHwWrite(uint8_t* p_data, uint32_t addr, uint32_t length)
{
  XSPI_RegularCmdTypeDef s_command;
  uint32_t end_addr;
  uint32_t current_addr;
  uint32_t current_size;


  // 페이지(256 B) 경계를 넘지 않게 나눠 쓴다
  current_size = W25Q256JV_PAGE_SIZE - (addr % W25Q256JV_PAGE_SIZE);
  if (current_size > length)
  {
    current_size = length;
  }
  current_addr = addr;
  end_addr     = addr + length;

  qspiCmdInit(&s_command, QSPI_CMD_PROG_QUAD_4B);
  s_command.AddressMode  = HAL_XSPI_ADDRESS_1_LINE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.DataMode     = HAL_XSPI_DATA_4_LINES;

  do
  {
    s_command.Address    = current_addr;
    s_command.DataLength = current_size;

    if (qspiWriteEnable() != QSPI_OK)
    {
      return QSPI_ERROR;
    }
    if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
      return QSPI_ERROR;
    }
    if (HAL_XSPI_Transmit(&hqspi, p_data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
      return QSPI_ERROR;
    }
    if (qspiAutoPollingMemReady(HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
    {
      return QSPI_ERROR;
    }

    current_addr += current_size;
    p_data       += current_size;
    current_size  = ((current_addr + W25Q256JV_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25Q256JV_PAGE_SIZE;
  } while (current_addr < end_addr);

  return QSPI_OK;
}

uint8_t qspiHwErase(uint8_t cmd, uint32_t addr, uint32_t timeout)
{
  XSPI_RegularCmdTypeDef s_command;


  qspiCmdInit(&s_command, cmd);
  s_command.AddressMode  = HAL_XSPI_ADDRESS_1_LINE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address      = addr;

  if (qspiWriteEnable() != QSPI_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (qspiAutoPollingMemReady(timeout) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiHwEraseChip(void)
{
  XSPI_RegularCmdTypeDef s_command;


  qspiCmdInit(&s_command, BULK_ERASE_CMD);

  if (qspiWriteEnable() != QSPI_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (qspiAutoPollingMemReady(W25Q256JV_BULK_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiHwGetID(uint8_t *p_id, uint32_t length)
{
  XSPI_RegularCmdTypeDef s_command;


  qspiCmdInit(&s_command, READ_ID_CMD);
  s_command.DataMode   = HAL_XSPI_DATA_1_LINE;
  s_command.DataLength = length;

  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Receive(&hqspi, p_id, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiHwEnableMemoryMappedMode(void)
{
  XSPI_RegularCmdTypeDef   s_command;
  XSPI_MemoryMappedTypeDef s_mem_mapped_cfg = {0};


  /* HAL_XSPI_MemoryMapped() 는 읽기(READ_CFG) 와 쓰기(WRITE_CFG) 명령이 **둘 다** 설정된 상태(CMD_CFG)
     에서만 진행한다. 읽기만 설정하면 READ_CMD_CFG 에 머물러 실패한다.
     참고한 H7R 드라이버는 hqspi.State 를 CMD_CFG 로 덮어써서 넘겼다. 여기서는 쓰기 명령도 정식으로 설정한다.
     메모리 맵으로 쓰지는 않는다 (qspiWrite 가 메모리 맵을 잠깐 풀고 명령으로 쓴다). */
  qspiCmdInit(&s_command, QSPI_CMD_READ_QUAD_IO_4B);
  s_command.OperationType       = HAL_XSPI_OPTYPE_READ_CFG;
  s_command.AddressMode         = HAL_XSPI_ADDRESS_4_LINES;
  s_command.AddressWidth        = HAL_XSPI_ADDRESS_32_BITS;
  s_command.AlternateBytesMode  = HAL_XSPI_ALT_BYTES_4_LINES;
  s_command.AlternateBytesWidth = HAL_XSPI_ALT_BYTES_8_BITS;
  s_command.AlternateBytes      = 0;
  s_command.DataMode            = HAL_XSPI_DATA_4_LINES;
  s_command.DummyCycles         = W25Q256JV_DUMMY_CYCLES_READ_QUAD;

  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  qspiCmdInit(&s_command, QSPI_CMD_PROG_QUAD_4B);
  s_command.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
  s_command.AddressMode   = HAL_XSPI_ADDRESS_1_LINE;
  s_command.AddressWidth  = HAL_XSPI_ADDRESS_32_BITS;
  s_command.DataMode      = HAL_XSPI_DATA_4_LINES;
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  // 일정 시간 접근이 없으면 CS 를 올린다 (칩이 저전력으로 갈 수 있게)
  s_mem_mapped_cfg.TimeOutActivation  = HAL_XSPI_TIMEOUT_COUNTER_ENABLE;
  s_mem_mapped_cfg.TimeoutPeriodClock = 0x20;
  if (HAL_XSPI_MemoryMapped(&hqspi, &s_mem_mapped_cfg) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

// 명령 하나를 1 선 명령 + 주소 / 데이터 없음으로 초기화한다. 호출부가 필요한 필드만 바꾼다.
void qspiCmdInit(XSPI_RegularCmdTypeDef *p_cmd, uint8_t instruction)
{
  memset(p_cmd, 0, sizeof(XSPI_RegularCmdTypeDef));

  p_cmd->OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
  p_cmd->Instruction        = instruction;
  p_cmd->InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
  p_cmd->InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
  p_cmd->InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  p_cmd->AddressMode        = HAL_XSPI_ADDRESS_NONE;
  p_cmd->AddressWidth       = HAL_XSPI_ADDRESS_24_BITS;
  p_cmd->AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_DISABLE;
  p_cmd->AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  p_cmd->DataMode           = HAL_XSPI_DATA_NONE;
  p_cmd->DataDTRMode        = HAL_XSPI_DATA_DTR_DISABLE;
  p_cmd->DummyCycles        = 0;
  p_cmd->DQSMode            = HAL_XSPI_DQS_DISABLE;
}

uint8_t qspiResetMemory(void)
{
  XSPI_RegularCmdTypeDef s_command;


  if (HAL_XSPI_GetState(&hqspi) != HAL_XSPI_STATE_READY)
  {
    HAL_XSPI_Abort(&hqspi);
  }

  qspiCmdInit(&s_command, RESET_ENABLE_CMD);
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  qspiCmdInit(&s_command, RESET_MEMORY_CMD);
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  // tRST 30 us 동안 명령을 받지 않는다
  delayUs(50);

  if (qspiAutoPollingMemReady(HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiWriteEnable(void)
{
  XSPI_RegularCmdTypeDef  s_command;
  XSPI_AutoPollingTypeDef s_config = {0};


  qspiCmdInit(&s_command, WRITE_ENABLE_CMD);
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  // WEL 이 설 때까지 SR1 을 폴링한다
  qspiCmdInit(&s_command, READ_STATUS_REG_CMD);
  s_command.DataMode   = HAL_XSPI_DATA_1_LINE;
  s_command.DataLength = 1;
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  s_config.MatchValue    = W25Q256JV_SR_WREN;
  s_config.MatchMask     = W25Q256JV_SR_WREN;
  s_config.MatchMode     = HAL_XSPI_MATCH_MODE_AND;
  s_config.IntervalTime  = 0x10;
  s_config.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
  if (HAL_XSPI_AutoPolling(&hqspi, &s_config, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiAutoPollingMemReady(uint32_t timeout)
{
  XSPI_RegularCmdTypeDef  s_command;
  XSPI_AutoPollingTypeDef s_config = {0};


  qspiCmdInit(&s_command, READ_STATUS_REG_CMD);
  s_command.DataMode   = HAL_XSPI_DATA_1_LINE;
  s_command.DataLength = 1;
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  s_config.MatchValue    = 0;
  s_config.MatchMask     = W25Q256JV_SR_WIP;
  s_config.MatchMode     = HAL_XSPI_MATCH_MODE_AND;
  s_config.IntervalTime  = 0x10;
  s_config.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
  if (HAL_XSPI_AutoPolling(&hqspi, &s_config, timeout) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiReadStatus(uint8_t cmd, uint8_t *p_data)
{
  XSPI_RegularCmdTypeDef s_command;


  qspiCmdInit(&s_command, cmd);
  s_command.DataMode   = HAL_XSPI_DATA_1_LINE;
  s_command.DataLength = 1;

  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Receive(&hqspi, p_data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t qspiWriteStatus(uint8_t cmd, uint8_t data)
{
  XSPI_RegularCmdTypeDef s_command;


  qspiCmdInit(&s_command, cmd);
  s_command.DataMode   = HAL_XSPI_DATA_1_LINE;
  s_command.DataLength = 1;

  if (qspiWriteEnable() != QSPI_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Command(&hqspi, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (HAL_XSPI_Transmit(&hqspi, &data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  if (qspiAutoPollingMemReady(HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

void HAL_XSPI_MspInit(XSPI_HandleTypeDef* xspiHandle)
{
  GPIO_InitTypeDef         GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit   = {0};


  if (xspiHandle->Instance == OCTOSPI1)
  {
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_OSPI;
    PeriphClkInit.OspiClockSelection   = RCC_OSPICLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_OSPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /**OCTOSPI1 GPIO Configuration (회로도 QSPI1_*, AF 는 데이터시트 AF 표)
    PA3     ------> OCTOSPI1_CLK   AF3
    PB10    ------> OCTOSPI1_NCS   AF9
    PC3     ------> OCTOSPI1_IO0   AF9
    PB0     ------> OCTOSPI1_IO1   AF6
    PC2     ------> OCTOSPI1_IO2   AF9
    PA1     ------> OCTOSPI1_IO3   AF9
    */
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    GPIO_InitStruct.Pin       = GPIO_PIN_3;
    GPIO_InitStruct.Alternate = GPIO_AF3_OCTOSPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_1;
    GPIO_InitStruct.Alternate = GPIO_AF9_OCTOSPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_10;
    GPIO_InitStruct.Alternate = GPIO_AF9_OCTOSPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_0;
    GPIO_InitStruct.Alternate = GPIO_AF6_OCTOSPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Alternate = GPIO_AF9_OCTOSPI1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  }
}

void HAL_XSPI_MspDeInit(XSPI_HandleTypeDef* xspiHandle)
{
  if (xspiHandle->Instance == OCTOSPI1)
  {
    __HAL_RCC_OSPI1_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1 | GPIO_PIN_3);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0 | GPIO_PIN_10);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_2 | GPIO_PIN_3);
  }
}



#if CLI_USE(HW_QSPI)
void cliCmd(cli_args_t *args)
{
  bool     ret = false;
  uint32_t addr;
  uint32_t length;
  uint32_t pre_time;
  bool     flash_ret;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    qspi_info_t info;

    qspiGetInfo(&info);
    cliPrintf("qspi init       : %s\n", is_init ? "OK" : "Fail");
    if (qspiGetXipMode() == true)
      cliPrintf("qspi id         : - (XIP 중에는 명령을 보내지 않는다)\n");
    else
      cliPrintf("qspi id         : %02X %02X %02X\n", info.device_id[0], info.device_id[1], info.device_id[2]);
    cliPrintf("qspi size       : %d MB\n", (int)(info.FlashSize/1024/1024));
    cliPrintf("qspi xip addr   : 0x%X\n", (unsigned int)qspiGetAddr());
    cliPrintf("qspi xip mode   : %s\n", qspiGetXipMode() ? "ON" : "OFF");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "xip"))
  {
    flash_ret = qspiSetXipMode(args->isStr(1, "on"));
    cliPrintf("qspiSetXipMode() : %s\n", flash_ret ? "OK" : "Fail");
    cliPrintf("qspi xip mode    : %s\n", qspiGetXipMode() ? "ON" : "OFF");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint8_t data;

    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    for (uint32_t i=0; i<length; i++)
    {
      if (qspiRead(addr+i, &data, 1) != true)
      {
        cliPrintf("\nreadFail : 0x%X\n", (unsigned int)(addr+i));
        break;
      }
      if ((i % 16) == 0)
        cliPrintf("\n0x%08X : ", (unsigned int)(addr+i));
      cliPrintf("%02X ", data);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "erase"))
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    pre_time  = millis();
    flash_ret = qspiErase(addr, length);
    cliPrintf("erase 0x%X %d : %s, %d ms\n", (unsigned int)addr, (int)length,
              flash_ret ? "OK" : "Fail", (int)(millis()-pre_time));
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write"))
  {
    uint32_t data;

    addr = (uint32_t)args->getData(1);
    data = (uint32_t)args->getData(2);

    pre_time  = millis();
    flash_ret = qspiWrite(addr, (uint8_t *)&data, 4);
    cliPrintf("write 0x%X 0x%X : %s, %d ms\n", (unsigned int)addr, (unsigned int)data,
              flash_ret ? "OK" : "Fail", (int)(millis()-pre_time));
    ret = true;
  }

  // 지우고 패턴을 쓰고 다시 읽어 비교한다. 쓰기/읽기 속도도 잰다.
  if (args->argc == 3 && args->isStr(0, "test"))
  {
    static uint32_t buf[1024/4];
    uint32_t err_cnt = 0;
    uint32_t t_erase, t_write, t_read;

    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);
    length = (length + sizeof(buf) - 1) & ~(sizeof(buf) - 1);

    pre_time = millis();
    flash_ret = qspiErase(addr, length);
    t_erase = millis() - pre_time;

    pre_time = millis();
    for (uint32_t ofs = 0; ofs < length && flash_ret; ofs += sizeof(buf))
    {
      for (uint32_t i = 0; i < sizeof(buf)/4; i++) buf[i] = (addr + ofs + i*4) ^ 0xA5A5A5A5;
      flash_ret = qspiWrite(addr + ofs, (uint8_t *)buf, sizeof(buf));
    }
    t_write = millis() - pre_time;

    pre_time = millis();
    for (uint32_t ofs = 0; ofs < length && flash_ret; ofs += sizeof(buf))
    {
      flash_ret = qspiRead(addr + ofs, (uint8_t *)buf, sizeof(buf));
      for (uint32_t i = 0; i < sizeof(buf)/4; i++)
      {
        if (buf[i] != ((addr + ofs + i*4) ^ 0xA5A5A5A5)) err_cnt++;
      }
    }
    t_read = millis() - pre_time;

    cliPrintf("test 0x%X %d KB : %s, err %d\n", (unsigned int)addr, (int)(length/1024),
              flash_ret ? "OK" : "Fail", (int)err_cnt);
    cliPrintf("  erase %d ms, write %d ms, read %d ms\n", (int)t_erase, (int)t_write, (int)t_read);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "speed"))
  {
    static uint32_t buf[512/4];
    uint32_t exe_time;

    cliPrintf("XIP : %s\n", qspiGetXipMode() ? "ON" : "OFF");

    pre_time = millis();
    for (int i=0; i<1024*1024/512; i++)
    {
      if (qspiRead(i*512, (uint8_t *)buf, 512) == false)
      {
        cliPrintf("qspiRead() Fail:%d\n", i);
        break;
      }
    }
    exe_time = millis()-pre_time;
    if (exe_time > 0)
    {
      cliPrintf("%d KB/sec\n", (int)(1024 * 1000 / exe_time));
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qspi info\n");
    cliPrintf("qspi xip on:off\n");
    cliPrintf("qspi speed\n");
    cliPrintf("qspi read  [addr] [length]\n");
    cliPrintf("qspi erase [addr] [length]\n");
    cliPrintf("qspi write [addr] [data]\n");
    cliPrintf("qspi test  [addr] [length]\n");
  }
}
#endif

#endif
