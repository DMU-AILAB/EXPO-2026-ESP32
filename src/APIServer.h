/**
 * @file APIServer.h
 * @brief ESP32-CAM 음성유도기 REST API 웹 서버
 *
 * ESPAsyncWebServer 기반의 비동기 HTTP 서버를 제공합니다.
 * PC YOLO 클라이언트 및 대시보드와의 통신을 담당합니다.
 */
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class APIServer {
public:
    /**
     * @brief HTTP 서버를 초기화하고 모든 API 엔드포인트를 등록합니다.
     * @param port 리스닝 포트 (기본값: 80)
     */
    static void begin(uint16_t port = 80);

    /** @brief 서버가 실행 중인지 확인합니다. */
    static bool isRunning();

private:
    static AsyncWebServer* _server;
    static bool            _running;

    // ── 핸들러 등록 메서드 ──────────────────────────────────────────────────
    static void setupCorsHeaders(AsyncWebServerResponse* response);
    static void handleOptionsRequest(AsyncWebServerRequest* req);

    // GET /api/status
    static void handleStatus(AsyncWebServerRequest* req);
    // GET /api/files
    static void handleFiles(AsyncWebServerRequest* req);
    // POST /api/play
    static void handlePlay(AsyncWebServerRequest* req, uint8_t* data,
                           size_t len, size_t idx, size_t total);
    // POST /api/stop
    static void handleStop(AsyncWebServerRequest* req);
    // POST /api/volume
    static void handleVolume(AsyncWebServerRequest* req, uint8_t* data,
                             size_t len, size_t idx, size_t total);
    // GET /api/rf/sensitivity
    static void handleRfSensitivityGet(AsyncWebServerRequest* req);
    // POST /api/rf/sensitivity
    static void handleRfSensitivityPost(AsyncWebServerRequest* req, uint8_t* data,
                                        size_t len, size_t idx, size_t total);
    // GET /api/rf/rssi
    static void handleRfRssi(AsyncWebServerRequest* req);
};
