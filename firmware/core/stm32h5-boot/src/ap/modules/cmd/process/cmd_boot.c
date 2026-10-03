#include "process/cmd_boot.h"
#include "boot/boot.h"
#include "util_core.h"


/*
 * 부트로더 커맨드 셋. stm32n6-boot 와 명령 코드, 응답 구조가 같다 (호스트 툴을 함께 쓴다).
 *
 * 이 보드는 단순형 레이아웃이라 대상은 FW 하나다 (BOOT / DATA 는 WRONG_RANGE).
 * 부트로더는 내장 플래시의 자기 자리(BOOT)에서 돌기 때문에 자기를 고쳐 쓸 수 없다.
 *
 *   FW   FLASH_ADDR_FIRM (TAG + 앱)
 *        BEGIN : TAG 가 든 첫 섹터를 지운다 (즉시 무효. 끊겨도 옛 앱을 실행하지 않는다)
 *        ERASE : TAG + 크기를 섹터(8 KB) 단위로
 *        WRITE : 벡터부터. 오프셋은 16 B(쿼드워드) 정렬이어야 한다
 *        END   : CRC 를 계산하고 비어 있는 TAG 자리에 쓴다 = 커밋
 *
 * H5 의 섹터는 8 KB 라 TAG(1 KB) 가 앱 첫 섹터 안에 있다. N6(외부 NOR, TAG 가 독립 4 KB 섹터)처럼
 * END 에서 TAG 만 지우고 다시 쓸 수 없다. ERASE 가 비워 둔 TAG 자리에 쓰기만 한다.
 *
 * END 는 [size:4][crc:4] 를 돌려준다 (CRC-16 utilCalcCRC). 호스트가 자기 계산과 비교한다.
 */
#define BOOT_CMD_INFO             0x0000
#define BOOT_CMD_VERSION          0x0001
#define BOOT_CMD_FW_BEGIN         0x0002    // [size:4] [target:1] [offset:4 (DATA)]
#define BOOT_CMD_FW_ERASE         0x0003
#define BOOT_CMD_FW_WRITE         0x0004    // [offset:4] [data]
#define BOOT_CMD_FW_READ          0x0005    // [offset:4] [len:4] [target:1]
#define BOOT_CMD_FW_END           0x0006    // -> [size:4] [crc:4]
#define BOOT_CMD_FW_VERIFY        0x0007
#define BOOT_CMD_FW_UPDATE        0x0008
#define BOOT_CMD_FW_JUMP          0x0009
#define BOOT_CMD_BAUD             0x0020    // [baud:4] -> 응답 뒤 보율을 바꾼다 (UART)
#define BOOT_CMD_RESET            0x0021

#define BOOT_TARGET_FW            0
#define BOOT_TARGET_BOOT          1
#define BOOT_TARGET_DATA          2

#define BOOT_CMD_VER              1         // boot_info_t 뒤에 붙인 확장의 판
#define BOOT_READ_MAX             512
#define BOOT_BUF_SIZE             1024
#define BOOT_SECTOR_SIZE          0x2000    // H5 내장 플래시 섹터 8 KB
#define BOOT_WRITE_ALIGN          16        // 쿼드워드
#define BOOT_BAUD_MIN             9600
#define BOOT_BAUD_MAX             12500000


/*
 * INFO 응답. 앞 104 바이트는 weact 와 같다. 그 뒤에 이 보드의 확장을 붙였다.
 */
typedef struct
{
  uint32_t magic;
  uint32_t mode;              // HW_DEV_MODE_BOOT / HW_DEV_MODE_APP
  uint32_t boot_addr;
  uint32_t boot_size;
  uint32_t firm_addr;         // TAG 시작
  uint32_t firm_vec_addr;     // 앱 벡터 시작
  uint32_t firm_size;
  uint32_t tag_size;
  uint32_t max_fw_size;
  uint32_t family_id;
  char     name[32];
  char     version[32];

  uint32_t cmd_ver;           // 여기부터 확장
  uint32_t boot2_addr;        // 이 보드는 없다 (0)
  uint32_t data_addr;
  uint32_t data_size;
  uint32_t baud;              // 지금 보율
} __attribute__((packed)) boot_info_t;

typedef struct
{
  uint8_t  img_type;          // BootImgType_t
  uint8_t  rsv[3];
  uint32_t fw_size;
  uint32_t fw_crc;
  char     name[32];
  char     version[32];
} __attribute__((packed)) boot_version_t;


static uint16_t cmdBootBegin(uint8_t *p_data, uint32_t length);
static uint16_t cmdBootErase(void);
static uint16_t cmdBootEnd(uint32_t *p_size, uint32_t *p_crc);
static uint16_t cmdBootEndFw(uint32_t *p_size, uint32_t *p_crc);
static bool     cmdBootCrc(uint32_t addr, uint32_t length, uint16_t *p_crc);
static uint32_t cmdBootWriteBase(void);

//-- FW_BEGIN ~ FW_END 사이의 전송 상태
static uint8_t  wr_target = BOOT_TARGET_FW;
static int32_t  wr_length = -1;    // 호스트가 신고한 크기. -1 이면 전송 중이 아니다
static uint32_t wr_index  = 0;     // 기록된 최대 끝 오프셋
static uint32_t wr_offset = 0;     // DATA 의 영역 안 시작 오프셋

static uint8_t  buf[BOOT_BUF_SIZE];




bool cmdBootProcess(cmd_t *p_cmd)
{
  uint16_t  cmd      = p_cmd->packet.cmd;
  uint16_t  err_code = OK;
  uint8_t  *p_data   = p_cmd->packet.data;
  uint32_t  length   = p_cmd->packet.length;


  switch (cmd)
  {
    case BOOT_CMD_INFO:
    {
      boot_info_t info;

      memset(&info, 0, sizeof(info));
      info.magic         = MAGIC_NUMBER;
      info.mode          = HW_DEV_MODE;
      info.boot_addr     = FLASH_ADDR_BOOT;
      info.boot_size     = FLASH_SIZE_BOOT;
      info.firm_addr     = FLASH_ADDR_FIRM;
      info.firm_vec_addr = FLASH_ADDR_FIRM_VEC;
      info.firm_size     = FLASH_SIZE_FIRM;
      info.tag_size      = FLASH_SIZE_TAG;
      info.max_fw_size   = FLASH_SIZE_FIRM - FLASH_SIZE_TAG;
      info.cmd_ver       = BOOT_CMD_VER;
      info.boot2_addr    = 0;
      info.data_addr     = 0;
      info.data_size     = 0;
      info.baud          = cmdGetBaud(p_cmd);                // 보율이 없는 채널이면 0
      snprintf(info.name,    sizeof(info.name),    "%s", _DEF_BOARD_NAME);
      snprintf(info.version, sizeof(info.version), "%s", _DEF_FIRMWATRE_VERSION);

      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&info, sizeof(info));
      break;
    }

    case BOOT_CMD_VERSION:
    {
      boot_version_t ver;
      firm_ver_t     fw_ver;
      firm_tag_t     tag;

      memset(&ver, 0, sizeof(ver));
      ver.img_type = (uint8_t)bootVerifyFirm();

      if (bootGetTag(&tag))
      {
        ver.fw_size = tag.fw_size;
        ver.fw_crc  = tag.fw_crc;
      }
      if (bootGetVer(&fw_ver))
      {
        //-- 플래시에서 읽은 문자열은 NUL 종료가 보장되지 않는다.
        memcpy(ver.name,    fw_ver.name_str,    sizeof(ver.name));
        memcpy(ver.version, fw_ver.version_str, sizeof(ver.version));
        ver.name[sizeof(ver.name)-1]       = 0;
        ver.version[sizeof(ver.version)-1] = 0;
      }
      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&ver, sizeof(ver));
      break;
    }

    case BOOT_CMD_FW_BEGIN:
      err_code = cmdBootBegin(p_data, length);
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;

    case BOOT_CMD_FW_ERASE:
      err_code = cmdBootErase();
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;

    case BOOT_CMD_FW_WRITE:
    {
      uint32_t offset = 0;

      if (length < 4 || wr_length <= 0)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        uint32_t n = length - 4;

        memcpy(&offset, &p_data[0], 4);

        if ((offset + n) > (uint32_t)wr_length || (offset % BOOT_WRITE_ALIGN) != 0)
        {
          err_code = ERR_BOOT_WRONG_RANGE;
        }
        else if (flashWrite(cmdBootWriteBase() + offset, &p_data[4], n) != true)
        {
          err_code = ERR_BOOT_FLASH_WRITE;
        }
        else if ((offset + n) > wr_index)
        {
          wr_index = offset + n;
        }
      }
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_READ:
    {
      uint32_t offset = 0;
      uint32_t len    = 0;
      uint8_t  target = BOOT_TARGET_FW;
      uint32_t base   = FLASH_ADDR_FIRM;
      uint32_t size   = FLASH_SIZE_FIRM;

      if (length < 8)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&offset, &p_data[0], 4);
        memcpy(&len,    &p_data[4], 4);
        if (length >= 9) target = p_data[8];

        // FW 는 TAG 부터 (weact 와 같다), BOOT 는 부트로더 자리
        if (target == BOOT_TARGET_BOOT) { base = FLASH_ADDR_BOOT; size = FLASH_SIZE_BOOT; }

        if (target > BOOT_TARGET_BOOT || len > BOOT_READ_MAX || offset > size || len > size - offset)
          err_code = ERR_BOOT_WRONG_RANGE;
        else if (flashRead(base + offset, buf, len) != true)
          err_code = ERR_BOOT_FLASH_READ;
      }

      if (err_code == OK)
        cmdSendResp(p_cmd, cmd, OK, buf, len);
      else
        cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_END:
    {
      uint32_t resp[2] = {0, 0};

      err_code = cmdBootEnd(&resp[0], &resp[1]);
      wr_length = -1;

      if (err_code == OK)
        cmdSendResp(p_cmd, cmd, OK, (uint8_t *)resp, sizeof(resp));
      else
        cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_VERIFY:
    {
      uint8_t img = (uint8_t)bootVerifyFirm();

      if (img == BOOT_IMG_NONE) err_code = ERR_BOOT_INVALID_FW;

      cmdSendResp(p_cmd, cmd, err_code, &img, 1);
      break;
    }

    case BOOT_CMD_FW_UPDATE:
    case BOOT_CMD_FW_JUMP:
      // 부트로더에서는 앱으로 점프한다. 앱이 없으면 점프하지 않고 오류를 돌려준다
      if (bootVerifyFirm() != BOOT_IMG_TAG)
      {
        cmdSendResp(p_cmd, cmd, ERR_BOOT_INVALID_FW, NULL, 0);
      }
      else
      {
        cmdSendResp(p_cmd, cmd, OK, NULL, 0);
        delay(50);                // 응답이 나갈 시간
        bootJumpFirm();
      }
      break;

    case BOOT_CMD_BAUD:
    {
      uint32_t baud = 0;

      if (length < 4)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&baud, &p_data[0], 4);
        if (cmdGetBaud(p_cmd) == 0)
          err_code = ERR_BOOT_WRONG_CMD;          // 보율이 있는 채널(UART)에서만 의미가 있다
        else if (baud < BOOT_BAUD_MIN || baud > BOOT_BAUD_MAX)
          err_code = ERR_BOOT_WRONG_RANGE;
      }

      // 응답은 지금 보율로 다 보낸 뒤에 바꾼다 (uartWrite 는 전송 완료까지 기다린다)
      cmdSendResp(p_cmd, cmd, err_code, (uint8_t *)&baud, 4);
      if (err_code == OK)
      {
        cmdSetBaud(p_cmd, baud);
      }
      break;
    }

    case BOOT_CMD_RESET:
      cmdSendResp(p_cmd, cmd, OK, NULL, 0);
      delay(50);
      resetToReset();
      break;

    default:
      cmdSendResp(p_cmd, cmd, ERR_BOOT_WRONG_CMD, NULL, 0);
      break;
  }

  return true;
}

uint16_t cmdBootBegin(uint8_t *p_data, uint32_t length)
{
  uint32_t size   = 0;
  uint8_t  target = BOOT_TARGET_FW;


  if (length < 4)
    return ERR_BOOT_WRONG_CMD;

  memcpy(&size, &p_data[0], 4);
  if (length >= 5) target = p_data[4];

  if (target != BOOT_TARGET_FW)
    return ERR_BOOT_WRONG_RANGE;      // 단순형 레이아웃은 FW 만 받는다

  if (size == 0 || size > FLASH_SIZE_FIRM - FLASH_SIZE_TAG)
    return ERR_BOOT_WRONG_RANGE;

  /*
   * TAG 가 든 첫 섹터를 **먼저** 지운다. 전송이 중간에 끊겨도 TAG 가 무효라 옛 이미지를
   * 실행하지 않는다. 이 섹터에는 앱 벡터도 있어 어차피 다시 쓴다.
   */
  if (flashErase(FLASH_ADDR_FIRM, BOOT_SECTOR_SIZE) != true)
    return ERR_BOOT_FLASH_ERASE;

  wr_target = target;
  wr_length = (int32_t)size;
  wr_index  = 0;
  wr_offset = 0;
  logPrintf("[  ] fw begin %d bytes\n", (int)size);
  return OK;
}

uint16_t cmdBootErase(void)
{
  uint32_t len;


  if (wr_length <= 0 || wr_target != BOOT_TARGET_FW)
    return ERR_BOOT_WRONG_CMD;

  // TAG 를 포함해 섹터(8 KB) 단위로 넓힌다
  len = FLASH_SIZE_TAG + (uint32_t)wr_length;
  len = (len + BOOT_SECTOR_SIZE - 1) & ~(BOOT_SECTOR_SIZE - 1);
  if (len > FLASH_SIZE_FIRM) len = FLASH_SIZE_FIRM;

  return flashErase(FLASH_ADDR_FIRM, len) ? OK : ERR_BOOT_FLASH_ERASE;
}

uint16_t cmdBootEnd(uint32_t *p_size, uint32_t *p_crc)
{
  if (wr_length <= 0 || wr_index == 0 || wr_target != BOOT_TARGET_FW)
    return ERR_BOOT_WRONG_CMD;

  return cmdBootEndFw(p_size, p_crc);
}

uint16_t cmdBootEndFw(uint32_t *p_size, uint32_t *p_crc)
{
  /*
   * TAG 를 마지막에 쓴다. 이게 커밋 마커다.
   *
   * 크기는 **`firm_ver_t.firm_size` 를 우선한다.** 호스트가 패딩을 붙이면 wr_index 가
   * 이미지 크기와 어긋나 다음 판정에서 stale tag 가 된다 (weact 에서 겪었다).
   */
  firm_tag_t tag;
  firm_ver_t ver;
  uint32_t   fw_size = wr_index;
  uint16_t   crc     = 0;


  if (bootGetVer(&ver) == true && ver.firm_size > 0 && ver.firm_size <= wr_index)
  {
    fw_size = ver.firm_size;
  }

  if (cmdBootCrc(FLASH_ADDR_FIRM_VEC, fw_size, &crc) != true)
    return ERR_BOOT_FLASH_READ;

  memset(&tag, 0, sizeof(tag));
  tag.magic_number = TAG_MAGIC_NUMBER;
  tag.fw_addr      = FLASH_SIZE_TAG;
  tag.fw_size      = fw_size;
  tag.fw_crc       = crc;
  tag.tag_crc      = utilCalcCRC(0, (uint8_t *)&tag, sizeof(tag) - 4);

  // TAG 자리는 ERASE 에서 비워 두었다. 지우지 않고 쓴다 (첫 섹터를 지우면 앱 벡터까지 지워진다).
  // flashWrite() 가 빈 자리인지 확인하고 쿼드워드 꼬리를 0xFF 로 채운다.
  if (flashWrite(FLASH_ADDR_FIRM, (uint8_t *)&tag, sizeof(tag)) != true)
    return ERR_BOOT_FLASH_WRITE;

  *p_size = fw_size;
  *p_crc  = crc;
  logPrintf("[  ] fw end %d bytes (rx %d), crc 0x%04X\n", (int)fw_size, (int)wr_index, crc);
  return OK;
}

bool cmdBootCrc(uint32_t addr, uint32_t length, uint16_t *p_crc)
{
  uint16_t crc = 0;

  for (uint32_t i = 0; i < length; i += sizeof(buf))
  {
    uint32_t n = length - i;

    if (n > sizeof(buf)) n = sizeof(buf);
    if (flashRead(addr + i, buf, n) != true)
      return false;
    crc = utilCalcCRC(crc, buf, n);
  }
  *p_crc = crc;
  return true;
}

uint32_t cmdBootWriteBase(void)
{
  return FLASH_ADDR_FIRM_VEC;
}
