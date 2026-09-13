/**
 * @file StorageManager.h
 * @brief NVS(비휘발성 메모리) 기반 설정값 영구 저장/불러오기
 */
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class StorageManager {
public:
    /**
     * @brief NVS 스토리지를 초기화합니다.
     * @return 초기화 성공 시 true
     */
    static bool begin();

    /** @brief 현재 RSSI 임계값 레지스터 값을 NVS에서 불러옵니다. */
    static uint8_t loadRssiThreshold();

    /** @brief RSSI 임계값 레지스터 값을 NVS에 저장합니다. */
    static bool saveRssiThreshold(uint8_t regVal);

    /** @brief 볼륨(0~100)을 NVS에서 불러옵니다. */
    static uint8_t loadVolume();

    /** @brief 볼륨(0~100)을 NVS에 저장합니다. */
    static bool saveVolume(uint8_t volume);

    /** @brief RF 수신 시 재생할 음성 파일명을 NVS에서 불러옵니다. */
    static String loadRfAudioFile();

    /** @brief RF 수신 시 재생할 음성 파일명을 NVS에 저장합니다. */
    static bool saveRfAudioFile(const String& filename);

private:
    static Preferences _prefs;
};
