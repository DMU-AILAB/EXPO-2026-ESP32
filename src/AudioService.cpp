/**
 * @file AudioService.cpp
 * @brief PCM5102A I2S DAC WAV 재생 구현
 *
 * 동작 원리:
 *   1. play() 호출 시 파일 경로를 태스크 파라미터로 전달하고 FreeRTOS 태스크 생성
 *   2. audioTask()에서 WAV 헤더 파싱 → I2S 클럭 재설정 → DMA 스트리밍
 *   3. I2S DMA 버퍼 크기: 512 샘플 × 2채널 × 2바이트 = 2048 바이트
 *   4. 볼륨 조절: int16_t PCM 값에 (volume/100.0) 배율 적용 (소프트웨어 스케일)
 */
#include "AudioService.h"
#include "Config.h"
#include <driver/i2s.h>
#include <SPI.h>

// ── 정적 멤버 초기화 ──────────────────────────────────────────────────────────
AudioState   AudioService::_state       = AudioState::IDLE;
String       AudioService::_currentFile = "";
uint8_t      AudioService::_volume      = 80;
bool         AudioService::_ready       = false;
TaskHandle_t AudioService::_taskHandle  = nullptr;

// I2S DMA 버퍼 크기 (샘플 단위)
static constexpr size_t I2S_DMA_SAMPLES    = 512;
static constexpr size_t I2S_DMA_BUF_LEN   = I2S_DMA_SAMPLES * 4; // 2ch × 2bytes
static constexpr int    I2S_DMA_BUF_COUNT  = 4;

bool AudioService::begin() {
    // I2S 드라이버 설정
    i2s_config_t i2s_config = {};
    i2s_config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate          = AUDIO_SAMPLE_RATE;
    i2s_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count        = I2S_DMA_BUF_COUNT;
    i2s_config.dma_buf_len          = I2S_DMA_BUF_LEN;
    i2s_config.use_apll             = true;   // APLL로 정밀 클럭 생성
    i2s_config.tx_desc_auto_clear   = true;   // DMA 오류 시 자동 무음 출력

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num   = I2S_BCK_PIN;
    pin_config.ws_io_num    = I2S_LRCK_PIN;
    pin_config.data_out_num = I2S_DIN_PIN;
    pin_config.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr) != ESP_OK) {
        return false;
    }
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        return false;
    }
    i2s_zero_dma_buffer(I2S_PORT);

    _ready = true;
    return true;
}

bool AudioService::isReady() {
    return _ready;
}

AudioState AudioService::getState() {
    return _state;
}

String AudioService::getCurrentFile() {
    return _currentFile;
}

uint8_t AudioService::getVolume() {
    return _volume;
}

void AudioService::setVolume(uint8_t volume) {
    _volume = (volume > 100) ? 100 : volume;
}

void AudioService::stop() {
    _state = AudioState::STOPPING;
    // 태스크가 STOPPING 상태를 감지하고 자체 종료
}

void AudioService::pause() {
    if (_state == AudioState::PLAYING) {
        _state = AudioState::PAUSED;
        i2s_zero_dma_buffer(I2S_PORT);
    }
}

void AudioService::resume() {
    if (_state == AudioState::PAUSED) {
        _state = AudioState::PLAYING;
    }
}

bool AudioService::play(const String& filepath) {
    if (!_ready) return false;

    // 기존 재생 중지 및 태스크 종료 대기
    if (_state != AudioState::IDLE) {
        stop();
        uint32_t t = millis();
        while (_state != AudioState::IDLE && (millis() - t) < 2000) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    // 파일 경로를 힙에 복사 (태스크 파라미터로 전달)
    String* path = new String(filepath);
    _currentFile = filepath;
    _state       = AudioState::PLAYING;

    BaseType_t ret = xTaskCreatePinnedToCore(
        audioTask,
        "audioTask",
        8192,       // 스택 크기 (WAV 버퍼 포함)
        (void*)path,
        3,          // 우선순위 (카메라 태스크보다 높게)
        &_taskHandle,
        0           // Core 0 (Wi-Fi/BT와 같은 코어)
    );

    if (ret != pdPASS) {
        delete path;
        _state = AudioState::IDLE;
        return false;
    }
    return true;
}

/**
 * @brief FreeRTOS 오디오 스트리밍 태스크
 *
 * Core 0에서 실행되며 SD 카드에서 WAV 데이터를 읽어 I2S DMA로 스트리밍합니다.
 */
void AudioService::audioTask(void* param) {
    String* pathPtr = (String*)param;
    String   path   = *pathPtr;
    delete pathPtr;

    // SD 파일 열기
    File file = SD.open(path);
    if (!file) {
        _state       = AudioState::IDLE;
        _currentFile = "";
        vTaskDelete(nullptr);
        return;
    }

    // WAV 헤더 파싱
    WavHeader hdr = parseWavHeader(file);
    if (!hdr.valid) {
        file.close();
        _state       = AudioState::IDLE;
        _currentFile = "";
        vTaskDelete(nullptr);
        return;
    }

    // I2S 샘플 레이트를 WAV 파일에 맞게 재설정
    i2s_set_sample_rates(I2S_PORT, hdr.sampleRate);

    // PCM 데이터 DMA 스트리밍 루프
    static int16_t buf[I2S_DMA_SAMPLES * 2]; // 스테레오 버퍼
    size_t remaining = hdr.dataSize;

    while (remaining > 0 && _state != AudioState::STOPPING) {
        // 일시정지 처리
        while (_state == AudioState::PAUSED) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        if (_state == AudioState::STOPPING) break;

        size_t toRead = min(
            (size_t)(I2S_DMA_SAMPLES * (hdr.bitsPerSample / 8) * hdr.channels),
            remaining
        );
        size_t bytesRead = file.read((uint8_t*)buf, toRead);
        if (bytesRead == 0) break;

        // 모노 → 스테레오 변환 (PCM5102A는 스테레오 입력 필요)
        size_t samples = bytesRead / (hdr.bitsPerSample / 8);
        if (hdr.channels == 1) {
            for (int i = (int)samples - 1; i >= 0; i--) {
                buf[i * 2 + 1] = buf[i];
                buf[i * 2]     = buf[i];
            }
            samples *= 2;
        }

        writeI2sDma(buf, samples);
        remaining -= bytesRead;
    }

    file.close();
    i2s_zero_dma_buffer(I2S_PORT);
    _state       = AudioState::IDLE;
    _currentFile = "";
    vTaskDelete(nullptr);
}

/**
 * @brief WAV 파일 헤더(RIFF/PCM, 44바이트)를 파싱합니다.
 */
AudioService::WavHeader AudioService::parseWavHeader(File& file) {
    WavHeader hdr = {0, 0, 0, 0, false};
    uint8_t header[44];

    if (file.read(header, 44) != 44) return hdr;

    // "RIFF" 시그니처 확인
    if (header[0] != 'R' || header[1] != 'I' ||
        header[2] != 'F' || header[3] != 'F') return hdr;

    // "WAVE" 포맷 확인
    if (header[8] != 'W' || header[9] != 'A' ||
        header[10] != 'V' || header[11] != 'E') return hdr;

    hdr.channels      = *(uint16_t*)(header + 22);
    hdr.sampleRate    = *(uint32_t*)(header + 24);
    hdr.bitsPerSample = *(uint16_t*)(header + 34);
    hdr.dataSize      = *(uint32_t*)(header + 40);
    hdr.valid         = true;
    return hdr;
}

/**
 * @brief I2S DMA로 PCM 샘플을 기록하며 볼륨 스케일링을 적용합니다.
 */
void AudioService::writeI2sDma(const int16_t* src, size_t samples) {
    static int16_t scaledBuf[I2S_DMA_SAMPLES * 2];
    float vol = _volume / 100.0f;
    for (size_t i = 0; i < samples && i < (I2S_DMA_SAMPLES * 2); i++) {
        int32_t s = (int32_t)(src[i] * vol);
        scaledBuf[i] = (int16_t)max(-32768, min(32767, s));
    }

    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, scaledBuf, samples * sizeof(int16_t),
              &bytesWritten, portMAX_DELAY);
}

String AudioService::listFilesJson() {
    String json = "[";
    bool first  = true;

    File dir = SD.open(SD_AUDIO_DIR);
    if (!dir || !dir.isDirectory()) {
        return "[]";
    }

    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            if (name.endsWith(".wav") || name.endsWith(".WAV")) {
                if (!first) json += ",";
                json += "{\"name\":\"" + name +
                        "\",\"size\":" + String(entry.size()) + "}";
                first = false;
            }
        }
        entry.close();
    }
    dir.close();
    json += "]";
    return json;
}
