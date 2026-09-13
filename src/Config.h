/**
 * @file Config.h
 * @brief 시각장애인용 음성유도기 ESP32-CAM - 전역 설정 및 핀 정의
 *
 * 하드웨어 구성:
 *   - ESP32-CAM (AI-Thinker) + OV2640 카메라
 *   - PCM5102A I2S DAC (16-bit / 44.1kHz)
 *   - AS4432-SMD (Si4432) 358.5MHz RF 수신 모듈
 *   - MicroSD 카드 (FAT32, WAV 음성 파일 저장)
 */
#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi 설정
// ─────────────────────────────────────────────────────────────────────────────
#define WIFI_SSID           "YOUR_SSID"
#define WIFI_PASSWORD       "YOUR_PASSWORD"
#define WIFI_CONNECT_TIMEOUT_MS  15000

// ─────────────────────────────────────────────────────────────────────────────
// OV2640 카메라 핀 (AI-Thinker ESP32-CAM 표준)
// ─────────────────────────────────────────────────────────────────────────────
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1   // 소프트웨어 리셋 사용
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// ─────────────────────────────────────────────────────────────────────────────
// 공유 SPI 버스 핀 (MicroSD + AS4432 공용)
// ─────────────────────────────────────────────────────────────────────────────
#define SPI_SCK_PIN     14
#define SPI_MOSI_PIN    15
#define SPI_MISO_PIN     2

// 칩 선택 핀 (각 장치별 독립)
#define SD_CS_PIN       13  ///< MicroSD 카드 CS
#define RF_CS_PIN       12  ///< AS4432-SMD nSEL (CS) - 부팅 스트래핑 주의: 외부 10k 풀다운 필수

// ─────────────────────────────────────────────────────────────────────────────
// I2S DAC (PCM5102A) 핀
// ─────────────────────────────────────────────────────────────────────────────
// 주의: GPIO 1(U0TXD), GPIO 3(U0RXD)은 UART0과 공유됩니다.
// 펌웨어 업로드 완료 후 Serial.end()를 호출하고 I2S로 재할당됩니다.
// 런타임 디버깅은 Wi-Fi 콘솔(/api/status)을 사용하세요.
#define I2S_BCK_PIN      1  ///< PCM5102A BCK (비트 클럭) - U0TXD
#define I2S_LRCK_PIN     3  ///< PCM5102A LRCK (좌우 채널 클럭) - U0RXD
#define I2S_DIN_PIN      4  ///< PCM5102A DIN (데이터) - 온보드 플래시 LED 핀 공용

// I2S 포트 번호
#define I2S_PORT        I2S_NUM_0

// 오디오 기본 설정
#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_BIT_DEPTH         16
#define AUDIO_CHANNELS          2     ///< 스테레오 (PCM5102A는 항상 스테레오 출력)
#define AUDIO_COOLDOWN_MS       5000  ///< 동일 음원 연속 재생 방지 쿨다운 (ms)

// SD카드 파일 경로
#define SD_AUDIO_DIR            "/audio"
#define SD_REMOTE_AUDIO_FILE    "/audio/remote_guide.wav"

// ─────────────────────────────────────────────────────────────────────────────
// AS4432 (Si4432) RF 모듈 설정
// ─────────────────────────────────────────────────────────────────────────────
// 한국 시각장애인 음성유도기 표준 주파수: 358.5000 MHz (±500Hz 이하)
// Si4432 Low-Band 레지스터 산출:
//   Reg 0x75 (Band Select): 0x53 = (sb=0, hbsel=0, fb=11)
//   Reg 0x76 (FC High Byte): 0xD4  (fc = 54400 = 0xD480)
//   Reg 0x77 (FC Low Byte):  0x80
//   검증: f = 10*(11+24) + 54400*(10/64000) = 350 + 8.5 = 358.500000 MHz ✓
#define RF_FREQ_MHZ             358.5f
// Si4432 Low-Band 주파수 레지스터 (hbsel=0, 240~479.9MHz 대역)
// Reg 0x75: sb=0, hbsel=0 (Low Band), fb=11 (0b00001011 = 0x0B)
// 검증: f = 10*(11+24) + 54400*(10/64000) = 350 + 8.5 = 358.500000 MHz ✓
#define RF_REG_BAND             0x0B  ///< Reg 0x75: sb=0, hbsel=0(Low Band), fb=11
#define RF_REG_FC_HIGH          0xD4  ///< Reg 0x76: fc 상위 바이트 (fc=54400=0xD480)
#define RF_REG_FC_LOW           0x80  ///< Reg 0x77: fc 하위 바이트

// RSSI 기본 감도 임계값 (-120 + val*0.5 dBm)
// 기본값 -75 dBm: 0x5A = 90 → -120 + 90*0.5 = -75.0 dBm
#define RF_RSSI_THRESHOLD_DEFAULT  0x5A  ///< 기본 RSSI 임계값 레지스터 값
#define RF_RSSI_DBM_DEFAULT        -75   ///< 기본 RSSI 임계값 dBm

// RF 신호 디바운스 및 쿨다운
#define RF_DEBOUNCE_MS          150   ///< RF 신호 유효 인정 최소 지속 시간 (ms)
#define RF_COOLDOWN_MS          4000  ///< RF 수신 쿨다운 (ms) - 연속 재생 방지
#define RF_POLL_INTERVAL_MS     15    ///< RF 폴링 주기 (ms)

// NVS (비휘발성 메모리) 키 이름
#define NVS_NAMESPACE           "beacon_cfg"
#define NVS_KEY_RSSI_THR        "rssi_thr"
#define NVS_KEY_VOLUME          "volume"
#define NVS_KEY_RF_AUDIO_FILE   "rf_audio"
