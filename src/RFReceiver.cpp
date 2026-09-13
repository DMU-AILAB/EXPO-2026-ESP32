/**
 * @file RFReceiver.cpp
 * @brief Si4432 (AS4432-SMD) 358.5000 MHz RF 수신 드라이버 구현
 *
 * 주파수 설정 근거:
 *   한국 TTA 표준 TTAS.KO-06.0124 「시각장애인용 음성유도기」
 *   송신 주파수: 358.5000 MHz (±500 Hz 이하)
 *
 *   Si4432 Low-Band 주파수 공식 (hbsel=0, 240~479.9 MHz):
 *     f_carrier = 10 × (fb + 24) + (fc × 10 / 64000)  [MHz]
 *     fb = 11 → base = 10 × 35 = 350 MHz
 *     fc = 54400 → offset = 54400 × 10 / 64000 = 8.500000 MHz
 *     f_carrier = 350 + 8.5 = 358.500000 MHz ✓
 *
 * Si4432 RSSI 공식:
 *     RSSI_dBm = -120 + (RSSI_register × 0.5)
 */
#include "RFReceiver.h"
#include "Config.h"

// ── 정적 멤버 초기화 ──────────────────────────────────────────────────────────
bool         RFReceiver::_ready         = false;
uint8_t      RFReceiver::_rssiThreshold = RF_RSSI_THRESHOLD_DEFAULT;
void       (*RFReceiver::_callback)()   = nullptr;
TaskHandle_t RFReceiver::_taskHandle    = nullptr;

// SPI 통신 속도 (Si4432 최대 10 MHz)
static constexpr uint32_t SI4432_SPI_CLK = 5000000; // 5 MHz (안정성 우선)

// 감도 레벨 매핑 테이블 (Level 1~10 → RSSI 임계값)
// RSSI_dBm = -120 + (reg * 0.5)
// Level 1 (-50 dBm): 최저 감도, 매우 근거리에서만 수신
// Level 10 (-90 dBm): 최고 감도, 원거리 수신 가능
static const uint8_t LEVEL_TO_REG[11] = {
    0x00,  // Level 0 (미사용)
    0x8C,  // Level 1: -50 dBm (reg=140)
    0x82,  // Level 2: -55 dBm (reg=130)
    0x78,  // Level 3: -60 dBm (reg=120)
    0x6E,  // Level 4: -65 dBm (reg=110)
    0x64,  // Level 5: -70 dBm (reg=100)
    0x5A,  // Level 6: -75 dBm (reg=90) ← 기본값
    0x50,  // Level 7: -80 dBm (reg=80)
    0x46,  // Level 8: -85 dBm (reg=70)
    0x3C,  // Level 9: -88 dBm (reg=60) [근사]
    0x30,  // Level 10: -90 dBm (reg=48)
};

// ── SPI 헬퍼 함수 ────────────────────────────────────────────────────────────
void RFReceiver::writeReg(uint8_t reg, uint8_t val) {
    SPI.beginTransaction(SPISettings(SI4432_SPI_CLK, MSBFIRST, SPI_MODE0));
    digitalWrite(RF_CS_PIN, LOW);
    SPI.transfer(reg | 0x80); // 쓰기 플래그: MSB = 1
    SPI.transfer(val);
    digitalWrite(RF_CS_PIN, HIGH);
    SPI.endTransaction();
}

uint8_t RFReceiver::readReg(uint8_t reg) {
    SPI.beginTransaction(SPISettings(SI4432_SPI_CLK, MSBFIRST, SPI_MODE0));
    digitalWrite(RF_CS_PIN, LOW);
    SPI.transfer(reg & 0x7F); // 읽기 플래그: MSB = 0
    uint8_t val = SPI.transfer(0x00);
    digitalWrite(RF_CS_PIN, HIGH);
    SPI.endTransaction();
    return val;
}

// ── 소프트웨어 리셋 ───────────────────────────────────────────────────────────
void RFReceiver::softReset() {
    writeReg(Si4432Reg::OP_FUNC_CTRL1, 0x80); // SWRES 비트 설정
    delay(20); // 리셋 안정화 대기
    // 리셋 완료 대기 (INT_STATUS2의 ichiprdy 비트)
    uint32_t t = millis();
    while (!(readReg(Si4432Reg::INT_STATUS2) & 0x02) && (millis() - t < 1000)) {
        delay(5);
    }
}

// ── 358.5000 MHz 주파수 설정 ─────────────────────────────────────────────────
void RFReceiver::configureFrequency() {
    // Reg 0x75 (FREQ_BAND): sb=0, hbsel=0, fb=11 (Low-Band, base 350 MHz)
    writeReg(Si4432Reg::FREQ_BAND,      RF_REG_BAND);     // 0x53
    // Reg 0x76, 0x77 (NOM_CARR_FREQ): fc = 54400 = 0xD480
    writeReg(Si4432Reg::NOM_CARR_FREQ1, RF_REG_FC_HIGH);  // 0xD4
    writeReg(Si4432Reg::NOM_CARR_FREQ0, RF_REG_FC_LOW);   // 0x80
}

// ── 초기화 ───────────────────────────────────────────────────────────────────
bool RFReceiver::begin() {
    // GPIO 12 CS 핀 초기화 (부팅 완료 후 즉시 HIGH → Idle 상태)
    pinMode(RF_CS_PIN, OUTPUT);
    digitalWrite(RF_CS_PIN, HIGH);

    // SPI 버스 초기화 (SD 카드와 공유)
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    // Si4432 소프트웨어 리셋
    softReset();

    // 장치 ID 검증 (Device Type Register: 0x00 → 예상값 0x08)
    uint8_t devType = readReg(Si4432Reg::DEVICE_TYPE);
    if (devType != 0x08) {
        Serial.printf("[RF] Si4432 장치 ID 불일치: 0x%02X (예상: 0x08)\n", devType);
        return false;
    }

    uint8_t devVer = readReg(Si4432Reg::DEVICE_VERSION);
    Serial.printf("[RF] Si4432 감지됨 - Type: 0x%02X, Ver: 0x%02X\n",
                  devType, devVer);

    // ── 수신 모드 설정 ───────────────────────────────────────────────────────
    // 변조 방식: OOK (On-Off Keying) - 음성유도기 리모컨 표준
    writeReg(Si4432Reg::MOD_MODE_CTRL2, 0x21); // txdtrtscale=0, fifo모드, OOK
    writeReg(Si4432Reg::OOK_CTR,        0x60); // OOK 피크 디텍터 활성화

    // 358.5000 MHz 주파수 설정
    configureFrequency();

    // 기본 RSSI 임계값 설정
    writeReg(Si4432Reg::RSSI_THRESHOLD, _rssiThreshold);

    // 수신 모드 진입 (RX 활성화)
    // OP_FUNC_CTRL1: rxon=1, xton=1 → 0x05
    writeReg(Si4432Reg::OP_FUNC_CTRL1, 0x05);
    delay(10);

    // 인터럽트 상태 클리어
    readReg(Si4432Reg::INT_STATUS1);
    readReg(Si4432Reg::INT_STATUS2);

    _ready = true;
    Serial.printf("[RF] AS4432 초기화 완료 - %.4f MHz 수신 대기 중\n",
                  RF_FREQ_MHZ);
    return true;
}

bool RFReceiver::isReady() {
    return _ready;
}

// ── RSSI 측정 ────────────────────────────────────────────────────────────────
uint8_t RFReceiver::readRssiReg() {
    return readReg(Si4432Reg::RSSI);
}

int8_t RFReceiver::readRssiDbm() {
    uint8_t val = readRssiReg();
    return (int8_t)(-120 + (val / 2));
}

// ── 감도 제어 ─────────────────────────────────────────────────────────────────
void RFReceiver::setRssiThreshold(uint8_t regVal) {
    _rssiThreshold = regVal;
    writeReg(Si4432Reg::RSSI_THRESHOLD, regVal);
    Serial.printf("[RF] RSSI 임계값 설정: reg=0x%02X (%.1f dBm)\n",
                  regVal, -120.0f + (regVal * 0.5f));
}

uint8_t RFReceiver::setRssiThresholdDbm(int8_t dbm) {
    // dBm → 레지스터 값 변환: reg = (dBm + 120) * 2
    int regValInt = (dbm + 120) * 2;
    regValInt = max(0, min(255, regValInt));
    uint8_t regVal = (uint8_t)regValInt;
    setRssiThreshold(regVal);
    return regVal;
}

RfSensitivityLevel RFReceiver::setSensitivityLevel(uint8_t level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;

    uint8_t regVal = LEVEL_TO_REG[level];
    setRssiThreshold(regVal);

    RfSensitivityLevel result;
    result.level     = level;
    result.rssiRegVal = regVal;
    result.rssiDbm   = (int8_t)(-120 + (regVal / 2));
    return result;
}

uint8_t RFReceiver::levelToRssiReg(uint8_t level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    return LEVEL_TO_REG[level];
}

uint8_t RFReceiver::getCurrentRssiThreshold() {
    return _rssiThreshold;
}

String RFReceiver::getSensitivityJson() {
    int8_t   dbm          = (int8_t)(-120 + (_rssiThreshold / 2));
    uint8_t  currentLevel = 6; // 기본값
    for (uint8_t l = 1; l <= 10; l++) {
        if (LEVEL_TO_REG[l] == _rssiThreshold) {
            currentLevel = l;
            break;
        }
    }
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"level\":%u,\"threshold_dbm\":%d,\"threshold_reg\":"
             "\"0x%02X\",\"frequency_mhz\":%.4f}",
             currentLevel, dbm, _rssiThreshold, RF_FREQ_MHZ);
    return String(buf);
}

// ── RF 폴링 태스크 ────────────────────────────────────────────────────────────
void RFReceiver::startTask(void (*callback)()) {
    _callback = callback;
    xTaskCreatePinnedToCore(
        rfTask,
        "rfTask",
        4096,
        nullptr,
        2,    // 우선순위 (오디오 태스크보다 낮게)
        &_taskHandle,
        0     // Core 0
    );
}

/**
 * @brief RF 신호 폴링 태스크
 *
 * 15ms 주기로 RSSI를 읽어 임계값 이상인 경우 150ms 디바운스 후 콜백을 호출합니다.
 * 콜백 호출 후 4초간 쿨다운을 적용하여 연속 재생을 방지합니다.
 */
void RFReceiver::rfTask(void* param) {
    uint32_t signalStartMs = 0;
    bool     signalActive  = false;
    uint32_t lastTriggerMs = 0;
    bool     inCooldown    = false;

    while (true) {
        uint8_t  rssiReg       = readRssiReg();
        bool     aboveThreshold = (rssiReg >= _rssiThreshold);
        uint32_t now           = millis();

        if (inCooldown && (now - lastTriggerMs > RF_COOLDOWN_MS)) {
            inCooldown = false;
        }

        if (!inCooldown) {
            if (aboveThreshold) {
                if (!signalActive) {
                    signalActive  = true;
                    signalStartMs = now;
                } else if ((now - signalStartMs) >= RF_DEBOUNCE_MS) {
                    // 유효 신호 감지! 콜백 호출
                    if (_callback) {
                        _callback();
                    }
                    Serial.printf("[RF] 신호 감지! RSSI=0x%02X (%.1f dBm)\n",
                                  rssiReg, -120.0f + (rssiReg * 0.5f));
                    signalActive  = false;
                    lastTriggerMs = now;
                    inCooldown    = true;
                }
            } else {
                signalActive = false;
            }
        }

        vTaskDelay(RF_POLL_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}
