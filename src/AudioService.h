/**
 * @file AudioService.h
 * @brief PCM5102A I2S DAC를 통한 WAV 파일 비동기 오디오 재생 서비스
 */
#pragma once
#include <Arduino.h>
#include <SD.h>

/** @brief 오디오 재생 상태 머신 */
enum class AudioState {
    IDLE,       ///< 재생 없음
    PLAYING,    ///< 재생 중
    PAUSED,     ///< 일시 정지
    STOPPING    ///< 정지 요청됨 (페이드 아웃 중)
};

class AudioService {
public:
    /**
     * @brief I2S DAC 및 SD 카드를 초기화합니다.
     * @return 초기화 성공 시 true
     */
    static bool begin();

    /**
     * @brief SD 카드에서 WAV 파일을 비동기로 재생합니다.
     * @param filepath SD 카드 내 WAV 파일 경로 (예: "/audio/cane_detected.wav")
     * @return 재생 요청 성공 시 true (실제 재생은 FreeRTOS 태스크에서 수행)
     */
    static bool play(const String& filepath);

    /** @brief 현재 재생을 즉시 중지합니다. */
    static void stop();

    /** @brief 현재 재생을 일시 정지합니다. */
    static void pause();

    /** @brief 일시 정지된 재생을 재개합니다. */
    static void resume();

    /**
     * @brief 볼륨을 설정합니다.
     * @param volume 0~100 범위의 볼륨 값
     */
    static void setVolume(uint8_t volume);

    /** @brief 현재 볼륨 값을 반환합니다. (0~100) */
    static uint8_t getVolume();

    /** @brief 현재 재생 상태를 반환합니다. */
    static AudioState getState();

    /** @brief 현재 재생 중인 파일명을 반환합니다. */
    static String getCurrentFile();

    /**
     * @brief SD 카드의 /audio 디렉토리에서 WAV 파일 목록을 반환합니다.
     * @return JSON 배열 형식의 파일 목록 문자열
     */
    static String listFilesJson();

    /** @brief 오디오 서비스가 초기화되었는지 확인합니다. */
    static bool isReady();

private:
    static AudioState   _state;
    static String       _currentFile;
    static uint8_t      _volume;       ///< 0~100
    static bool         _ready;
    static TaskHandle_t _taskHandle;

    /** @brief WAV 헤더 파싱 결과 */
    struct WavHeader {
        uint32_t sampleRate;
        uint16_t bitsPerSample;
        uint16_t channels;
        uint32_t dataSize;
        bool     valid;
    };

    /** @brief WAV 파일 헤더(44바이트)를 파싱합니다. */
    static WavHeader parseWavHeader(File& file);

    /** @brief FreeRTOS 오디오 재생 태스크 (Core 0에서 실행) */
    static void audioTask(void* param);

    /** @brief I2S DMA 버퍼로 PCM 데이터를 전송하며 볼륨 스케일을 적용합니다. */
    static void writeI2sDma(const int16_t* src, size_t samples);
};
