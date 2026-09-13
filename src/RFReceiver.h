/**
 * @file RFReceiver.h
 * @brief AS4432-SMD (Si4432 기반) 358.5000 MHz RF 수신 모듈 드라이버
 *
 * 한국 시각장애인 음성유도기 표준 주파수(358.5000 MHz ±500Hz)를 수신합니다.
 * SPI 인터페이스로 Si4432 레지스터를 직접 제어합니다.
 */
#pragma once
#include <Arduino.h>
#include <SPI.h>

/** @brief Si4432 주요 레지스터 주소 */
namespace Si4432Reg {
    static constexpr uint8_t DEVICE_TYPE       = 0x00;
    static constexpr uint8_t DEVICE_VERSION    = 0x01;
    static constexpr uint8_t DEVICE_STATUS     = 0x02;
    static constexpr uint8_t INT_STATUS1       = 0x03;
    static constexpr uint8_t INT_STATUS2       = 0x04;
    static constexpr uint8_t INT_ENABLE1       = 0x05;
    static constexpr uint8_t INT_ENABLE2       = 0x06;
    static constexpr uint8_t OP_FUNC_CTRL1     = 0x07;
    static constexpr uint8_t OP_FUNC_CTRL2     = 0x08;
    static constexpr uint8_t CRYSTAL_OSC_LOAD  = 0x09;
    static constexpr uint8_t RSSI              = 0x26;
    static constexpr uint8_t RSSI_THRESHOLD    = 0x27;
    static constexpr uint8_t OOK_CTR           = 0x42;
    static constexpr uint8_t MOD_MODE_CTRL2    = 0x71;
    static constexpr uint8_t FREQ_BAND         = 0x75;
    static constexpr uint8_t NOM_CARR_FREQ1    = 0x76;
    static constexpr uint8_t NOM_CARR_FREQ0    = 0x77;
}

/** @brief RF 수신 감도 레벨 정보 */
struct RfSensitivityLevel {
    uint8_t  level;        ///< 1(최저 감도) ~ 10(최고 감도)
    uint8_t  rssiRegVal;  ///< RSSI 임계값 레지스터 값
    int8_t   rssiDbm;     ///< dBm 환산 값
};

class RFReceiver {
public:
    /**
     * @brief AS4432-SMD 모듈을 초기화합니다.
     * @return 초기화 성공 시 true (Si4432 장치 ID 검증 포함)
     */
    static bool begin();

    /**
     * @brief FreeRTOS RF 폴링 태스크를 시작합니다. (Core 0)
     * @param callback RF 신호 감지 시 호출될 콜백 함수
     */
    static void startTask(void (*callback)());

    /** @brief 현재 RSSI 레지스터 값을 읽습니다. */
    static uint8_t readRssiReg();

    /** @brief 현재 RSSI를 dBm으로 반환합니다. */
    static int8_t readRssiDbm();

    /**
     * @brief RSSI 감도 임계값을 레지스터 값으로 설정합니다.
     * @param regVal Si4432 RSSI 레지스터 값 (dBm = -120 + regVal*0.5)
     */
    static void setRssiThreshold(uint8_t regVal);

    /**
     * @brief RSSI 감도 임계값을 dBm으로 설정합니다.
     * @param dbm 목표 감도 (예: -75)
     * @return 실제 적용된 레지스터 값
     */
    static uint8_t setRssiThresholdDbm(int8_t dbm);

    /**
     * @brief 감도 레벨(1~10)으로 RSSI 임계값을 설정합니다.
     * @param level 1(최저 감도/근거리만 수신) ~ 10(최고 감도/원거리 수신)
     * @return 설정된 감도 레벨 정보
     */
    static RfSensitivityLevel setSensitivityLevel(uint8_t level);

    /** @brief 현재 RSSI 감도 임계값 레지스터 값을 반환합니다. */
    static uint8_t getCurrentRssiThreshold();

    /** @brief 현재 감도 레벨 정보를 JSON으로 반환합니다. */
    static String getSensitivityJson();

    /** @brief RF 수신 모듈이 정상 초기화되었는지 확인합니다. */
    static bool isReady();

private:
    static bool         _ready;
    static uint8_t      _rssiThreshold;   ///< 현재 RSSI 임계값 레지스터 값
    static void       (*_callback)();      ///< 신호 감지 콜백
    static TaskHandle_t _taskHandle;

    /** @brief SPI를 통해 Si4432 레지스터에 쓰기 */
    static void writeReg(uint8_t reg, uint8_t val);

    /** @brief SPI를 통해 Si4432 레지스터 읽기 */
    static uint8_t readReg(uint8_t reg);

    /** @brief Si4432 소프트웨어 리셋 */
    static void softReset();

    /** @brief 358.5000 MHz 주파수 레지스터를 설정합니다. */
    static void configureFrequency();

    /** @brief RF 폴링 FreeRTOS 태스크 */
    static void rfTask(void* param);

    /** @brief 감도 레벨 1~10을 RSSI 레지스터 값으로 변환 */
    static uint8_t levelToRssiReg(uint8_t level);
};
