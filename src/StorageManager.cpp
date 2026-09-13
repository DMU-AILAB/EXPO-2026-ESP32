/**
 * @file StorageManager.cpp
 * @brief NVS Preferences 영구 설정 저장 구현
 */
#include "StorageManager.h"

Preferences StorageManager::_prefs;

bool StorageManager::begin() {
    return _prefs.begin(NVS_NAMESPACE, false);
}

uint8_t StorageManager::loadRssiThreshold() {
    return _prefs.getUChar(NVS_KEY_RSSI_THR, RF_RSSI_THRESHOLD_DEFAULT);
}

bool StorageManager::saveRssiThreshold(uint8_t regVal) {
    _prefs.putUChar(NVS_KEY_RSSI_THR, regVal);
    return true;
}

uint8_t StorageManager::loadVolume() {
    return _prefs.getUChar(NVS_KEY_VOLUME, 80);
}

bool StorageManager::saveVolume(uint8_t volume) {
    _prefs.putUChar(NVS_KEY_VOLUME, volume);
    return true;
}

String StorageManager::loadRfAudioFile() {
    return _prefs.getString(NVS_KEY_RF_AUDIO_FILE, SD_REMOTE_AUDIO_FILE);
}

bool StorageManager::saveRfAudioFile(const String& filename) {
    _prefs.putString(NVS_KEY_RF_AUDIO_FILE, filename);
    return true;
}
