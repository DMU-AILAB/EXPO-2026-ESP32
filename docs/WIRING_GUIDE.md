# 하드웨어 배선 가이드

## 목차
1. [부품 목록](#부품-목록)
2. [ESP32-CAM 핀맵 개요](#esp32-cam-핀맵-개요)
3. [공유 SPI 버스 구조](#공유-spi-버스-구조)
4. [PCM5102A I2S DAC 배선](#pcm5102a-i2s-dac-배선)
5. [AS4432-SMD RF 모듈 배선](#as4432-smd-rf-모듈-배선)
6. [MicroSD 카드 (온보드)](#microsd-카드-온보드)
7. [아두이노 우노로 펌웨어 업로드](#아두이노-우노로-펌웨어-업로드)
8. [전원 공급 설계](#전원-공급-설계)
9. [완성 배선 요약표](#완성-배선-요약표)

---

## 부품 목록

| 부품 | 수량 | 비고 |
|---|---|---|
| ESP32-CAM (AI-Thinker) | 1 | OV2640 카메라 내장 |
| PCM5102A DAC 모듈 | 1 | 3.3V/5V 호환 |
| AS4432-SMD RF 모듈 | 1 | Si4432 기반, 358.5MHz 안테나 내장 |
| MicroSD 카드 | 1 | FAT32 포맷, 32GB 이하 권장 |
| 아두이노 우노 | 1 | 펌웨어 업로드용 UART 브리지 |
| 저항 10kΩ | 1 | GPIO 12 풀다운 (필수!) |
| 전해 커패시터 100µF / 16V | 2 | 전원 노이즈 억제 |
| 세라믹 커패시터 100nF | 4 | 바이패스 커패시터 |
| 스피커 앰프 모듈 (PAM8403 등) | 1 | PCM5102A → 스피커 구동 |
| 스피커 (4Ω 또는 8Ω) | 1 | 3W 이상 권장 |

---

## ESP32-CAM 핀맵 개요

```
                  ┌─────────────────────────────┐
       (5V 입력) ─┤ 5V        GND ├─ (GND)
                  │                              │
    [카메라 PWDN]─┤ GPIO 32   GPIO 0 ├─ [XCLK / 부팅모드]
    [카메라 SIOD]─┤ GPIO 26   GPIO 2 ├─ [SPI MISO / SD]
    [카메라 SIOC]─┤ GPIO 27   GPIO 4 ├─ [I2S DIN / Flash LED]
    [카메라 D7]  ─┤ GPIO 35   GPIO 12├─ [RF CS (10k 풀다운)]
    [카메라 D6]  ─┤ GPIO 34   GPIO 13├─ [SD CS]
    [카메라 D5]  ─┤ GPIO 39   GPIO 14├─ [SPI SCK]
    [카메라 D4]  ─┤ GPIO 36   GPIO 15├─ [SPI MOSI]
    [카메라 D3]  ─┤ GPIO 21   GPIO 1 ├─ [I2S BCK / U0TXD]
    [카메라 D2]  ─┤ GPIO 19   GPIO 3 ├─ [I2S LRCK / U0RXD]
    [카메라 D1]  ─┤ GPIO 18              │
    [카메라 D0]  ─┤ GPIO 5               │
    [카메라 VSYNC]┤ GPIO 25              │
    [카메라 HREF] ┤ GPIO 23              │
    [카메라 PCLK] ┤ GPIO 22              │
                  └─────────────────────────────┘
```

> [!NOTE]
> GPIO 16, 17은 PSRAM에 내부 연결되어 외부에서 사용 불가합니다.
> GPIO 33은 온보드 적색 LED(상태 표시용)에 연결되어 있습니다.

---

## 공유 SPI 버스 구조

MicroSD 카드 슬롯(온보드)과 AS4432-SMD RF 모듈은 **동일한 SPI 버스**를 공유합니다.
칩 선택(CS) 핀만 분리하여 동시 충돌을 방지합니다.

```
ESP32-CAM                SD Card (온보드)        AS4432-SMD RF 모듈
─────────                ────────────────        ──────────────────
GPIO 14 (SPI_SCK)  ────── CLK ─────────────────── SCK
GPIO 15 (SPI_MOSI) ────── MOSI ────────────────── SDI (MOSI)
GPIO  2 (SPI_MISO) ────── MISO ────────────────── SDO (MISO)
GPIO 13 (SD_CS)    ────── CS   (SD 전용)
GPIO 12 (RF_CS)    ──────────────────────────── nSEL (RF 전용)
                                                   + 10kΩ → GND (풀다운 필수!)
```

> [!CAUTION]
> **GPIO 12 (MTDI) 부팅 스트래핑 핀 경고**
>
> ESP32의 GPIO 12는 리셋 시 HIGH로 인식되면 내부 Flash 구동 전압을 **1.8V**로
> 설정하여 부팅 실패를 유발합니다 ("flash read err" 오류).
>
> 반드시 GPIO 12 ↔ GND 사이에 **10kΩ 풀다운 저항**을 장착하여 부팅 시
> LOW 상태를 보장하세요. 펌웨어 시작 후 소프트웨어에서 HIGH(Idle)로 전환합니다.

---

## PCM5102A I2S DAC 배선

### PCM5102A 모듈 점퍼 설정 (필수)

| 점퍼 | 연결 | 기능 |
|---|---|---|
| SCK | GND | 마스터 클럭 외부 입력 비활성화 (ESP32 APLL 사용) |
| FLT | GND | 필터 모드: 일반 지연 |
| DMP | GND | 디엠퍼시스 필터: 비활성 |
| FMT | GND | 오디오 포맷: I2S 표준 |
| XMT | 3.3V | 뮤트 비활성화 (오디오 출력 활성) |

> [!IMPORTANT]
> **SCK 핀을 반드시 GND에 연결하세요.**
> SCK를 연결하지 않으면 PCM5102A가 외부 마스터 클럭을 대기하여 무음 출력됩니다.
> ESP32의 APLL(Audio PLL)이 BCK/LRCK를 자동으로 공급합니다.

### 배선 연결표

| PCM5102A 핀 | ESP32-CAM 핀 | 신호명 |
|---|---|---|
| VCC | 3.3V | 전원 |
| GND | GND | 접지 |
| BCK | GPIO 1 (U0TXD) | 비트 클럭 |
| LRCK (WS) | GPIO 3 (U0RXD) | 좌우 채널 클럭 |
| DIN | GPIO 4 | PCM 데이터 입력 |
| SCK | GND | 마스터 클럭 (비활성) |
| LOUT | 스피커 앰프 L입력 | 좌채널 아날로그 출력 |
| ROUT | 스피커 앰프 R입력 | 우채널 아날로그 출력 |

> [!WARNING]
> **GPIO 1, 3은 UART0과 공유됩니다.**
> 펌웨어 업로드 및 시리얼 디버깅 중에는 PCM5102A를 분리하거나
> 오디오 재생을 하지 마세요. 업로드 완료 후 `Serial.end()`가 호출되면
> 자동으로 I2S로 전환됩니다.

### 스피커 앰프 연결 (PAM8403 예시)

```
PCM5102A LOUT/ROUT ──→ PAM8403 입력 (L/R)
PAM8403 출력 ──→ 스피커 (4Ω/3W 또는 8Ω/2W)
PAM8403 VCC ──→ 5V (별도 전원 권장)
PAM8403 GND ──→ GND (ESP32와 공통)
```

---

## AS4432-SMD RF 모듈 배선

### 핀 연결표

| AS4432-SMD 핀 | ESP32-CAM 핀 | 신호명 |
|---|---|---|
| VCC | 3.3V | 전원 (3.3V 전용, 5V 금지!) |
| GND | GND | 접지 |
| SCK | GPIO 14 | SPI 클럭 |
| SDI | GPIO 15 | SPI MOSI (데이터 입력) |
| SDO | GPIO 2 | SPI MISO (데이터 출력) |
| nSEL | GPIO 12 | 칩 선택 (CS, Active LOW) |
| nIRQ | 미연결 | 인터럽트 (폴링 방식 사용) |
| ANT | 외부 안테나 또는 온보드 | 358.5MHz 안테나 |

> [!NOTE]
> AS4432-SMD 모듈은 **3.3V 전용**입니다. 5V를 인가하면 모듈이 손상됩니다.
> nIRQ 핀은 본 펌웨어에서 폴링(polling) 방식으로 운용하므로 연결하지 않아도 됩니다.

### 안테나 설치

- **온보드 패턴 안테나**: AS4432-SMD에 내장된 PCB 패턴 안테나를 사용할 경우
  별도 외부 안테나 불필요
- **외부 안테나**: 수신 거리를 늘리려면 SMA 커넥터를 통해 **358MHz 1/4 파장 단극 안테나**
  (약 209mm) 또는 상용 안테나를 연결
- 한국 시각장애인 음성유도기 표준에 따라 수신 거리는 **2m~10m** 범위를 목표로 설계

---

## MicroSD 카드 (온보드)

ESP32-CAM의 온보드 MicroSD 슬롯을 사용합니다. 별도 배선 없이 소켓에 삽입하면 됩니다.

| 온보드 SD 신호 | 내부 연결 GPIO |
|---|---|
| CLK | GPIO 14 |
| MOSI | GPIO 15 |
| MISO | GPIO 2 |
| CS | GPIO 13 |

SD 카드 준비 방법은 [AUDIO_SD_GUIDE.md](./AUDIO_SD_GUIDE.md)를 참조하세요.

---

## 아두이노 우노로 펌웨어 업로드

CP2102 USB-UART 변환 모듈 없이 **아두이노 우노**를 UART 브리지로 활용하는 방법입니다.

### 원리
아두이노 우노의 ATmega328P를 리셋(RESET → GND 단락) 상태로 고정하면
온보드 USB-UART 칩(ATmega16U2)만 동작하여 순수 UART 브리지가 됩니다.

### 배선 방법

```
아두이노 우노                  ESP32-CAM
────────────                  ─────────
RESET ──── GND               (Uno 내부 단락)
5V    ──────────────────────→ 5V
GND   ──────────────────────→ GND
RX (D0) ────────────────────→ GPIO 1 (U0TXD, ESP32의 TX)
TX (D1) ────────────────────→ GPIO 3 (U0RXD, ESP32의 RX)
```

> [!IMPORTANT]
> **TX/RX 교차 연결**: 우노의 RX → ESP32의 TX (GPIO 1), 우노의 TX → ESP32의 RX (GPIO 3)

### 업로드 절차

1. **아두이노 우노 준비**:
   - Uno의 `RESET` 핀과 `GND` 핀을 점퍼 와이어로 단락합니다.
   - PC와 USB 케이블로 연결합니다.

2. **ESP32-CAM 플래시 모드 진입**:
   - ESP32-CAM의 `GPIO 0`을 `GND`에 연결합니다. (플래시 모드 진입 조건)

3. **ESP32-CAM 리셋**:
   - ESP32-CAM의 리셋 버튼을 눌렀다 놓습니다. (또는 전원을 껐다 켭니다.)

4. **펌웨어 업로드**:
   - PlatformIO: `platformio.ini`의 `upload_port`를 우노 COM 포트로 설정 후
     `Upload` 버튼 클릭 (또는 `pio run -t upload`)
   - Arduino IDE: 보드 = "AI Thinker ESP32-CAM", 포트 = 우노 COM 포트 선택 후 업로드

5. **업로드 완료 후**:
   - `GPIO 0`과 `GND`의 연결을 제거합니다.
   - ESP32-CAM을 리셋합니다.
   - 우노의 `RESET-GND` 단락 점퍼를 제거합니다. (선택)

> [!TIP]
> 업로드 속도 `115200 bps`가 기본값입니다. 불안정하면 `57600`으로 낮추세요.
> `platformio.ini`의 `upload_speed` 값을 수정하면 됩니다.

---

## 전원 공급 설계

### 권장 전원 구성

```
외부 5V/2A USB 전원 어댑터
         │
         ├──→ ESP32-CAM 5V 핀  (최대 500mA 소비)
         ├──→ 스피커 앰프 VCC   (최대 700mA @ 풀볼륨)
         └──→ 100µF 전해 커패시터 (GND 사이에 병렬)
```

### 3.3V 라인 (ESP32 내부 LDO)
```
ESP32 3.3V 출력
         ├──→ PCM5102A VCC  (약 20mA)
         ├──→ AS4432-SMD VCC (약 30mA)
         └──→ 100µF + 100nF 바이패스 커패시터
```

> [!WARNING]
> ESP32-CAM의 내장 LDO는 최대 **600mA** 출력입니다.
> 카메라(250mA) + RF 모듈(30mA) + DAC(20mA) + ESP32 자체(300mA) 합산 시
> Wi-Fi 송신 피크에서 한계에 근접합니다. 반드시 5V 외부 전원을 충분히 공급하세요.

---

## 완성 배선 요약표

| 연결 시작 | 연결 끝 | 신호 설명 |
|---|---|---|
| ESP32 GPIO 14 | SD CLK / AS4432 SCK | SPI 클럭 (공유) |
| ESP32 GPIO 15 | SD MOSI / AS4432 SDI | SPI MOSI (공유) |
| ESP32 GPIO  2 | SD MISO / AS4432 SDO | SPI MISO (공유) |
| ESP32 GPIO 13 | SD CS | SD 카드 칩 선택 |
| ESP32 GPIO 12 | AS4432 nSEL | RF 모듈 칩 선택 |
| ESP32 GPIO 12 | 10kΩ → GND | 부팅 스트래핑 풀다운 |
| ESP32 GPIO  1 | PCM5102A BCK | I2S 비트 클럭 |
| ESP32 GPIO  3 | PCM5102A LRCK | I2S 채널 클럭 |
| ESP32 GPIO  4 | PCM5102A DIN | I2S 데이터 |
| PCM5102A SCK | GND | 마스터 클럭 비활성 |
| PCM5102A XMT | 3.3V | 뮤트 해제 |
| 우노 RX (D0) | ESP32 GPIO 1 | 업로드 시 UART TX |
| 우노 TX (D1) | ESP32 GPIO 3 | 업로드 시 UART RX |
| 우노 RESET | GND (우노 내부) | ATmega328P 리셋 고정 |
| ESP32 GPIO 0 | GND (업로드 시만) | 플래시 모드 진입 |
