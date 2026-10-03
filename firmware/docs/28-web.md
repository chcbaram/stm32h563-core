# 28. 웹페이지 — 펌웨어 / 부트로더 업데이트

> 브라우저에서 보드를 연결해 앱(펌웨어)과 부트로더를 업데이트한다. GitHub Pages 로 배포한다.
> **<https://chcbaram.github.io/stm32h563-core/>**
> 관련: [26](26-bootloader.md) (cmd 프로토콜), [27](27-usb.md) (USB CDC / HID, VID/PID)
>
> **상태 (2026-10-03) : 만들어 배포했다. 브라우저에서 실제로 눌러 보는 시험은 아직이다** (5 절).

---

## 1. 무엇을 하나

| 기능 | 브라우저 API | 보드 쪽 |
|---|---|---|
| 펌웨어(앱) 업데이트 | **WebHID** 또는 **Web Serial** | 우리 부트로더의 cmd 프로토콜 (`download.py` 와 같은 흐름) |
| 부트로더 업데이트 | **WebUSB** (DfuSe) | STM32H5 **ROM 시스템 부트로더**의 USB DFU (`0483:DF11`) |

Chrome / Edge 가 필요하다. 셋 다 HTTPS 에서만 되고 github.io 가 그 조건을 채운다.

### 펌웨어 업데이트 흐름

1. **HID 로 연결** 또는 **CDC 로 연결** → INFO 로 보드 이름 / 버전 / 모드(BOOT·APP) 를 보여 준다
2. 저장소 이미지(보드 선택) 또는 파일을 고르고 **펌웨어 업데이트**
3. 앱이 돌고 있으면 `FW_UPDATE` → 보드가 부트로더로 리셋 → **USB 가 다시 열거되면 다시 찾아 붙는다**
4. BEGIN → ERASE → WRITE(1008 B 씩) → END(CRC 비교) → VERIFY → JUMP

부트로더(`1209:B563`)와 앱(`B565`)은 PID 가 달라 브라우저 권한도 따로다. **처음 한 번은** 보드가 부트로더로 바뀌었을 때
"부트로더 고르기" 버튼이 나오고 목록에서 `STM32H5-BOOT` 를 골라야 한다 (`requestDevice` 는 사용자 클릭 안에서만 된다).
그 뒤로는 브라우저가 기억해 `getDevices()` / `getPorts()` 로 바로 붙는다.

CDC 는 다른 프로그램(터미널)이 그 포트를 열고 있으면 열 수 없다. HID 는 터미널이 CDC 를 CLI 로 쓰는 중에도 된다 ([27](27-usb.md) 3 절).

### 부트로더 업데이트 흐름 (ROM DFU)

1. 보드의 **BOOT0 버튼(S3)** 을 누른 채 **리셋(S2)** → ROM 이 USB DFU 로 열거된다
2. **DFU 장치 연결 후 부트로더 쓰기** → 목록에서 `DFU in FS Mode`
3. DfuSe : 섹터 지우기(`0x41`) → 주소 지정(`0x21`) → 블록 쓰기(wBlockNum ≥ 2) → (선택) 다시 읽어 비교(UPLOAD) → 떠나기(0x0800_0000 에서 실행)

전송 크기는 DFU 기능 디스크립터의 `wTransferSize`, 지울 섹터는 인터페이스 이름의 메모리 배치(`@Internal Flash /0x08000000/…`)에서 읽는다.
읽지 못하면 8 KB 섹터로 가정한다. 앱 영역은 건드리지 않으므로 쓰고 나면 새 부트로더가 기존 앱으로 점프한다.

---

## 2. 파일

```
index.html                화면 (w6300 스타일)
.nojekyll                 Jekyll 을 거치지 않고 그대로 배포 (HAL / TinyUSB 소스가 많아서)
web/
├── proto.js              cmd 패킷, HidChannel / SerialChannel, 장치 찾기 / 다시 붙기
├── boot.js               부트로더 커맨드, INFO / VERSION 파싱, CRC-16, downloadFirmware()
├── dfu.js                ROM DFU (WebUSB DfuSe)
└── bin/                  저장소에 넣어 두는 이미지 — tools/release.py 가 만든다
    ├── manifest.json
    ├── boot/stm32h5-boot.bin
    └── core/stm32h5-fw.bin       (확장보드 펌웨어가 생기면 <보드>/stm32h5-fw.bin)
```

`proto.js` / `boot.js` 는 w6300 의 `web/` 을 바탕으로 했다. 바꾼 것:

| | w6300 | 여기 |
|---|---|---|
| 전송 | HID (+ 보드 웹서버 HTTP) | HID + **Web Serial (CDC)** |
| 응답 판정 | STX 만 찾는다 | **type = RESP 이고 cmd 가 같을 때만** (115200 CDC 에서 CLI 에코를 응답으로 오인하지 않게, `cmdproto.py` 와 같다) |
| INFO / VERSION | w6300 슬롯 구조 | N6 형식 (`boot_info_t` 104 + 20 B, `boot_version_t` 76 B) |
| 다시 붙기 | — | PID 가 바뀌는 재열거를 기다리고, 권한이 없으면 사용자에게 고르게 한다 |
| DFU | 없음 | `dfu.js` 새로 |

---

## 3. 저장소 이미지 — `tools/release.py`

```bash
python3 firmware/core/stm32h5-fw/tools/release.py
```

```
  boot/stm32h5-boot.bin        STM32H5-BOOT V261003R1  88360 B
  core/stm32h5-fw.bin          STM32H5-FW V261003R1  84840 B
manifest : web/bin/manifest.json
```

- 빌드 결과(`build/*.bin`)를 `web/bin/` 에 복사하고 `manifest.json` 을 만든다. 빌드는 하지 않는다
- 이름 / 버전은 bin 안의 `firm_ver_t` 에서 읽는다. `firm_addr` 가 레이아웃(부트로더 0x0800_0000, 앱 0x0804_0400)과 다르면 멈춘다
- manifest 에 크기, CRC-16, **SHA-256** 을 넣는다. 웹페이지는 받은 bin 의 SHA-256 을 확인하고 쓴다
- **펌웨어는 보드마다 하나**다. 모두 이름이 `stm32h5-fw.bin` 이라 보드별 폴더로 나눈다. 확장보드 펌웨어가 생기면 `FIRMWARES` 에 한 줄 더한다.
  웹페이지는 manifest 의 `firmwares` 목록으로 "저장소" 드롭다운을 채운다
- 매번 `web/bin/` 을 비우고 다시 만든다 (목록에서 빠진 보드의 옛 bin 이 남지 않게)
- 부트로더와 앱 프로젝트에 같은 파일을 둔다

```json
{
  "boot": { "file": "boot/stm32h5-boot.bin", "name": "STM32H5-BOOT", "version": "V261003R1", "addr": "0x08000000", "sha256": "…", "method": "dfu" },
  "firmwares": [
    { "id": "core", "title": "코어보드", "file": "core/stm32h5-fw.bin", "name": "STM32H5-FW", "version": "V261003R1", "addr": "0x08040400", "sha256": "…", "method": "cmd" }
  ]
}
```

---

## 4. GitHub Pages

w6300 과 같은 설정이다 — `main` 브랜치의 루트(`/`)를 그대로 배포 (GitHub Actions 없음).

```bash
gh api -X POST repos/chcbaram/stm32h563-core/pages -f "source[branch]=main" -f "source[path]=/"
```

루트에 빈 `.nojekyll` 을 두어 Jekyll 을 거치지 않는다. 저장소에 HAL / TinyUSB 소스가 많아 빌드가 느려지고,
Jekyll 은 `_` 로 시작하는 폴더도 빼 버린다.

---

## 5. 검증

### 한 것 — 브라우저 없이 로직 대조 (JavaScriptCore)

이 맥에는 Node.js 가 없어 macOS 의 JavaScriptCore(`jsc -m`) 로 모듈을 돌렸다. 입출력이 없어 보드와의 종단 시험은 못 하고, 로직을 Python 툴 / 실제 보드 응답과 대조했다.

| 시험 | 결과 |
|---|---|
| `buildPacket()` == Python `cmdproto.build()` (바이트 단위) | PASS |
| `crc16(bin)` == `release.py` 가 manifest 에 쓴 값 (부트로더 / 앱) | PASS |
| `parseInfo()` — 실제 보드 INFO (앱 / 부트로더) | PASS (`STM32H5-FW APP`, `STM32H5-BOOT BOOT max 457728`) |
| `parseVersion()` — 실제 보드 VERSION | PASS |
| `crc16(bin)` == 보드가 TAG 에 쓴 CRC (같은 이미지를 내려받은 뒤) | PASS (`0xB9B9`) |
| `readImageVer()` — bin 의 `firm_ver_t` | PASS |
| `parseMemoryLayout()` / 지울 섹터 계산 | PASS (부트로더 88 KB → 11 섹터) |
| `index.html` 스크립트 문법 (`checkModuleSyntax`), JS 가 찾는 요소 id 가 HTML 에 있는지 | OK |

> 처음 `crc16(bin) == 보드 TAG crc` 가 실패했다. 보드에 올라가 있던 앱이 다시 빌드하기 전의 것이었다
> (`__DATE__` / `__TIME__` 이 들어가 크기는 같고 내용이 다르다). 지금 bin 을 내려받은 뒤 같아졌다.

### 남은 것 — 브라우저에서

- [ ] HID 로 연결 → 펌웨어 업데이트 (앱 상태에서 시작, 부트로더 고르기 한 번)
- [ ] CDC 로 연결 → 펌웨어 업데이트
- [ ] ROM DFU → 부트로더 업데이트 (BOOT0 + 리셋). H5 ROM 의 인터페이스 이름 / 전송 크기 / 지우기 동작이 예상대로인지
- [ ] Windows 에서 (WebUSB 는 ROM DFU 장치에 WinUSB 드라이버가 필요할 수 있다)
