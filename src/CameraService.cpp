/**
 * @file CameraService.cpp
 * @brief OV2640 카메라 초기화, 프레임 캡처 및 MJPEG 스트리밍 구현
 *
 * 스트리밍 구조:
 *   - HTTP/1.1 multipart/x-mixed-replace 방식으로 MJPEG 스트림 제공
 *   - PSRAM 기반 더블 버퍼링(FB_COUNT = 2)으로 카메라 태스크와 스트리밍 동시 처리
 *   - 프레임 레이트: VGA(640x480) 기준 약 15~25 FPS (Wi-Fi 대역폭에 따라 변동)
 */
#include "CameraService.h"
#include "Config.h"
#include <esp_camera.h>
#include <Arduino.h>

bool CameraService::_ready = false;

// MJPEG 스트리밍 헤더
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY    = "\r\n--frame\r\n";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

bool CameraService::begin() {
    camera_config_t config;

    // ── 클럭 ──────────────────────────────────────────────────────────────
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.xclk_freq_hz = 20000000;  // 20 MHz XCLK

    // ── 핀 설정 ───────────────────────────────────────────────────────────
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_sscb_sda = CAM_PIN_SIOD;
    config.pin_sscb_scl = CAM_PIN_SIOC;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_pclk     = CAM_PIN_PCLK;

    // ── 이미지 포맷 및 버퍼 ───────────────────────────────────────────────
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        // PSRAM 있음: 고해상도, 더블 버퍼링
        config.frame_size   = FRAMESIZE_VGA;    // 640x480
        config.jpeg_quality = 12;              // 낮을수록 고품질 (0~63)
        config.fb_count     = 2;               // 더블 버퍼
    } else {
        // PSRAM 없음: 저해상도, 싱글 버퍼
        config.frame_size   = FRAMESIZE_QVGA;   // 320x240
        config.jpeg_quality = 20;
        config.fb_count     = 1;
    }

    // ── 카메라 초기화 ─────────────────────────────────────────────────────
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] 카메라 초기화 실패: 0x%x\n", err);
        return false;
    }

    // ── 이미지 품질 조정 ──────────────────────────────────────────────────
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_sharpness(s, 0);
        s->set_whitebal(s, 1);        // AWB 활성화
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);         // AWB 모드: 자동
        s->set_exposure_ctrl(s, 1);   // 자동 노출 활성화
        s->set_aec2(s, 0);
        s->set_aec_value(s, 300);
        s->set_gain_ctrl(s, 1);       // 자동 게인 활성화
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_bpc(s, 0);             // 블랙 픽셀 보정
        s->set_wpc(s, 1);             // 화이트 픽셀 보정
        s->set_raw_gma(s, 1);         // 감마 보정
        s->set_lenc(s, 1);            // 렌즈 보정
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);
    }

    _ready = true;
    Serial.println("[CAM] OV2640 초기화 완료");
    return true;
}

bool CameraService::isReady() {
    return _ready;
}

void CameraService::registerRoutes(AsyncWebServer& server) {
    server.on("/stream",  HTTP_GET, handleStream);
    server.on("/capture", HTTP_GET, handleCapture);
}

/**
 * @brief MJPEG 스트리밍 핸들러
 *
 * AsyncWebServer의 청크 응답(chunked response) 방식을 활용합니다.
 * 각 프레임은 MIME multipart 경계와 Content-Length 헤더와 함께 전송됩니다.
 *
 * @note AsyncWebServer의 특성상 단일 핸들러 호출당 한 프레임만 전송됩니다.
 *       PC 클라이언트는 연속 HTTP 연결을 유지하며 프레임을 수신해야 합니다.
 *       Python 클라이언트 예제: examples/pc_yolo_client.py 참조
 */
void CameraService::handleStream(AsyncWebServerRequest* request) {
    if (!_ready) {
        request->send(503, "application/json",
                      "{\"error\":\"Camera not ready\"}");
        return;
    }

    AsyncResponseStream* response =
        request->beginResponseStream(STREAM_CONTENT_TYPE);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Connection",    "keep-alive");

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        char partBuf[64];
        snprintf(partBuf, sizeof(partBuf), STREAM_PART, fb->len);
        response->print(STREAM_BOUNDARY);
        response->print(partBuf);
        response->write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
    }
    request->send(response);
}

/**
 * @brief 단일 JPEG 이미지 캡처 핸들러
 */
void CameraService::handleCapture(AsyncWebServerRequest* request) {
    if (!_ready) {
        request->send(503, "application/json",
                      "{\"error\":\"Camera not ready\"}");
        return;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        request->send(500, "application/json",
                      "{\"error\":\"Frame capture failed\"}");
        return;
    }

    AsyncWebServerResponse* response =
        request->beginResponse_P(200, "image/jpeg", fb->buf, fb->len);
    response->addHeader("Content-Disposition",
                        "inline; filename=\"capture.jpg\"");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);

    esp_camera_fb_return(fb);
}

String CameraService::getStatusJson() {
    if (!_ready) return "{\"camera\":\"offline\"}";

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"camera\":\"online\",\"resolution\":\"%s\",\"psram\":%s}",
             psramFound() ? "VGA(640x480)" : "QVGA(320x240)",
             psramFound() ? "true" : "false");
    return String(buf);
}
