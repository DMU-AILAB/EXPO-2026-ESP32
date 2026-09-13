/**
 * @file main.cpp
 * @brief 시각장애인용 스마트 음성유도기 - ESP32-CAM 펌웨어 진입점
 *
 * 시스템 구성:
 *   - ESP32-CAM (AI-Thinker) + OV2640 카메라
 *   - PCM5102A I2S DAC (16-bit 스테레오 WAV 재생)
 *   - AS4432-SMD (Si4432) 358.5000 MHz RF 리모컨 수신
 *   - MicroSD 카드 (FAT32, /audio 디렉토리 내 WAV 파일 저장)
 *
 * FreeRTOS 태스크 구조:
 *   Core 0: Wi-Fi/BT, RF 폴링 태스크, 오디오 스트리밍 태스크
 *   Core 1: setup()/loop() (카메라, API 서버)
 *
 * 업로드 방법: 아두이노 우노 UART 브리지 사용 (docs/WIRING_GUIDE.md 참조)
 *
 * @version 1.0.0
 * @date    2026-09
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>

#include "Config.h"
#include "StorageManager.h"
#include "CameraService.h"
#include "AudioService.h"
#include "RFReceiver.h"
#include "APIServer.h"

// ──────────────────────────────────────────────────────────────────────────────
// 전방 선언
// ──────────────────────────────────────────────────────────────────────────────
static void initWiFi();
static void initSD();
static void onRfSignalDetected();
static void printSystemInfo();

// ──────────────────────────────────────────────────────────────────────────────
// RF 신호 감지 콜백
// ──────────────────────────────────────────────────────────────────────────────
/**
 * @brief RF 358.5MHz 리모컨 신호 감지 시 호출되는 콜백
 *
 * RFReceiver FreeRTOS 태스크(Core 0)에서 호출됩니다.
 * NVS에 저장된 RF 안내 음성 파일을 재생합니다.
 */
static void onRfSignalDetected() {
    String rfAudioFile = StorageManager::loadRfAudioFile();
    if (rfAudioFile.isEmpty()) {
        rfAudioFile = SD_REMOTE_AUDIO_FILE; // 기본값
    }
    Serial.printf("[MAIN] RF 신호 → 음성 재생: %s\n", rfAudioFile.c_str());
    AudioService::play(rfAudioFile);
}

// ──────────────────────────────────────────────────────────────────────────────
// Wi-Fi 연결
// ──────────────────────────────────────────────────────────────────────────────
static void initWiFi() {
    Serial.printf("[WIFI] SSID: %s 연결 중...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startMs > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("[WIFI] 연결 시간 초과 - AP 모드로 전환 (설정 미완료)");
            // Wi-Fi 연결 실패 시에도 RF/오디오는 정상 동작
            return;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] 연결 완료 - IP: %s\n",
                  WiFi.localIP().toString().c_str());
}

// ──────────────────────────────────────────────────────────────────────────────
// SD 카드 초기화
// ──────────────────────────────────────────────────────────────────────────────
static void initSD() {
    // AS4432 CS를 HIGH(비활성)로 설정 후 SD CS 초기화
    pinMode(RF_CS_PIN, OUTPUT);
    digitalWrite(RF_CS_PIN, HIGH);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    // SPI 버스 초기화 (SD 카드 + AS4432 공유)
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] 초기화 실패 - SD 카드 삽입 및 배선을 확인하세요");
        return;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] 초기화 완료 - 용량: %llu MB\n", cardSize);

    // /audio 디렉토리 자동 생성
    if (!SD.exists(SD_AUDIO_DIR)) {
        SD.mkdir(SD_AUDIO_DIR);
        Serial.printf("[SD] '%s' 디렉토리 생성됨\n", SD_AUDIO_DIR);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// 시스템 정보 출력
// ──────────────────────────────────────────────────────────────────────────────
static void printSystemInfo() {
    Serial.println("==============================================");
    Serial.println(" 시각장애인용 음성유도기 ESP32-CAM v1.0.0");
    Serial.println("==============================================");
    Serial.printf(" Chip Model  : %s Rev%d\n",
                  ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf(" CPU Freq    : %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf(" Free Heap   : %u bytes\n", ESP.getFreeHeap());
    Serial.printf(" PSRAM       : %s (%u bytes)\n",
                  psramFound() ? "Found" : "Not Found",
                  ESP.getPsramSize());
    Serial.printf(" Flash Size  : %u MB\n", ESP.getFlashChipSize() / (1024*1024));
    Serial.println("==============================================");
}

// ──────────────────────────────────────────────────────────────────────────────
// setup() - 1회 실행 초기화 루틴
// ──────────────────────────────────────────────────────────────────────────────
void setup() {
    // UART 시리얼 초기화 (디버깅용, 업로드 완료 후 사용)
    // 주의: GPIO 1(TX), GPIO 3(RX)는 I2S DAC와 공유되므로
    //       begin() 이후 AudioService::begin() 전에 end()를 호출합니다.
    Serial.begin(115200);
    delay(200);
    printSystemInfo();

    // ── 1. NVS 영구 설정 불러오기 ──────────────────────────────────────────
    StorageManager::begin();
    uint8_t savedRssiThr = StorageManager::loadRssiThreshold();
    uint8_t savedVolume  = StorageManager::loadVolume();
    Serial.printf("[NVS] RSSI 임계값: 0x%02X, 볼륨: %u%%\n",
                  savedRssiThr, savedVolume);

    // ── 2. SD 카드 초기화 ──────────────────────────────────────────────────
    initSD();

    // ── 3. OV2640 카메라 초기화 ────────────────────────────────────────────
    if (!CameraService::begin()) {
        Serial.println("[MAIN] 카메라 초기화 실패 - 카메라 없이 계속 진행");
    }

    // ── 4. AS4432 RF 모듈 초기화 ───────────────────────────────────────────
    //    RF 모듈은 SPI 버스를 SD와 공유하므로 SD 초기화 후 진행
    if (RFReceiver::begin()) {
        // NVS에서 불러온 RSSI 임계값 적용
        RFReceiver::setRssiThreshold(savedRssiThr);
        // RF 폴링 태스크 시작 (Core 0)
        RFReceiver::startTask(onRfSignalDetected);
    } else {
        Serial.println("[MAIN] RF 모듈 초기화 실패 - RF 없이 계속 진행");
    }

    // ── 5. PCM5102A I2S DAC 초기화 ─────────────────────────────────────────
    //    Serial.end()로 UART0(GPIO 1, 3)을 해제 후 I2S에 재할당
    Serial.end();                   // GPIO 1, 3 → UART0 해제
    delay(10);
    if (!AudioService::begin()) {
        // I2S 초기화 실패 시 시리얼 재활성화는 하지 않음 (Wi-Fi 상태로 확인)
    }
    AudioService::setVolume(savedVolume);

    // ── 6. Wi-Fi 연결 ──────────────────────────────────────────────────────
    //    Serial.end() 이후이므로 Wi-Fi 상태는 /api/status로 확인
    initWiFi();

    // ── 7. REST API 웹 서버 시작 ────────────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
        APIServer::begin(80);
    }

    // ── 8. 시작 알림 음성 재생 ─────────────────────────────────────────────
    delay(500);
    if (SD.exists("/audio/startup.wav")) {
        AudioService::play("/audio/startup.wav");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// loop() - Core 1에서 반복 실행
// ──────────────────────────────────────────────────────────────────────────────
void loop() {
    // Wi-Fi 재연결 감시 (연결이 끊어진 경우)
    static uint32_t lastWifiCheckMs = 0;
    if (millis() - lastWifiCheckMs > 10000) {
        lastWifiCheckMs = millis();
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.reconnect();
        }
    }

    // loop()는 빠르게 반환하여 FreeRTOS 스케줄러를 방해하지 않음
    vTaskDelay(100 / portTICK_PERIOD_MS);
}
