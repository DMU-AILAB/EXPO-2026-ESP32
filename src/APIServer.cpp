/**
 * @file APIServer.cpp
 * @brief REST API 서버 구현
 *
 * 엔드포인트 목록:
 *   GET  /stream              - MJPEG 실시간 영상 스트림
 *   GET  /capture             - 단일 JPEG 정지영상
 *   GET  /api/status          - 시스템 전체 상태 조회
 *   GET  /api/files           - SD 카드 음성 파일 목록
 *   POST /api/play            - 음성 파일 재생
 *   POST /api/stop            - 재생 중지
 *   POST /api/volume          - 볼륨 조절
 *   GET  /api/rf/sensitivity  - RF 감도 조회
 *   POST /api/rf/sensitivity  - RF 감도 설정
 *   GET  /api/rf/rssi         - 현재 RF RSSI 조회
 */
#include "APIServer.h"
#include "Config.h"
#include "AudioService.h"
#include "RFReceiver.h"
#include "CameraService.h"
#include "StorageManager.h"
#include <ArduinoJson.h>
#include <WiFi.h>

AsyncWebServer* APIServer::_server  = nullptr;
bool            APIServer::_running = false;

static const char* CONTENT_JSON = "application/json";

void APIServer::setupCorsHeaders(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin",  "*");
    response->addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void APIServer::begin(uint16_t port) {
    _server = new AsyncWebServer(port);

    // CORS Preflight 및 404 처리
    _server->onNotFound([](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_OPTIONS) {
            AsyncWebServerResponse* r = req->beginResponse(204);
            r->addHeader("Access-Control-Allow-Origin",  "*");
            r->addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
            r->addHeader("Access-Control-Allow-Headers", "Content-Type");
            req->send(r);
        } else {
            req->send(404, CONTENT_JSON, "{\"error\":\"Not Found\"}");
        }
    });

    // ── 카메라 엔드포인트 등록 ────────────────────────────────────────────
    CameraService::registerRoutes(*_server);

    // ── REST API 엔드포인트 등록 ──────────────────────────────────────────
    _server->on("/api/status", HTTP_GET, handleStatus);
    _server->on("/api/files",  HTTP_GET, handleFiles);
    _server->on("/api/stop",   HTTP_POST, handleStop);
    _server->on("/api/rf/sensitivity", HTTP_GET, handleRfSensitivityGet);
    _server->on("/api/rf/rssi",        HTTP_GET, handleRfRssi);

    // POST 핸들러 (Body 있음)
    _server->on("/api/play", HTTP_POST, nullptr, nullptr, handlePlay);
    _server->on("/api/volume", HTTP_POST, nullptr, nullptr, handleVolume);
    _server->on("/api/rf/sensitivity", HTTP_POST, nullptr, nullptr,
                handleRfSensitivityPost);

    _server->begin();
    _running = true;
    Serial.printf("[API] 웹 서버 시작됨 - http://%s:%u\n",
                  WiFi.localIP().toString().c_str(), port);
}

bool APIServer::isRunning() {
    return _running;
}

/**
 * GET /api/status
 * 시스템 전체 상태 반환
 */
void APIServer::handleStatus(AsyncWebServerRequest* req) {
    StaticJsonDocument<512> doc;
    doc["wifi_ip"]       = WiFi.localIP().toString();
    doc["wifi_rssi_dbm"] = WiFi.RSSI();
    doc["free_heap"]     = ESP.getFreeHeap();
    doc["uptime_sec"]    = millis() / 1000;

    // 오디오 상태
    JsonObject audio = doc.createNestedObject("audio");
    audio["state"]   = (int)AudioService::getState();
    audio["file"]    = AudioService::getCurrentFile();
    audio["volume"]  = AudioService::getVolume();
    audio["ready"]   = AudioService::isReady();

    // RF 상태
    JsonObject rf = doc.createNestedObject("rf");
    rf["ready"]         = RFReceiver::isReady();
    rf["rssi_reg"]      = RFReceiver::readRssiReg();
    rf["rssi_dbm"]      = RFReceiver::readRssiDbm();
    rf["threshold_reg"] = RFReceiver::getCurrentRssiThreshold();
    rf["threshold_dbm"] = -120 + (RFReceiver::getCurrentRssiThreshold() / 2);
    rf["frequency_mhz"] = RF_FREQ_MHZ;

    // 카메라 상태
    doc["camera"] = CameraService::getStatusJson();

    String body;
    serializeJson(doc, body);

    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, body);
    setupCorsHeaders(response);
    req->send(response);
}

/**
 * GET /api/files
 * SD 카드 음성 파일 목록 반환
 */
void APIServer::handleFiles(AsyncWebServerRequest* req) {
    String body = AudioService::listFilesJson();
    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, body);
    setupCorsHeaders(response);
    req->send(response);
}

/**
 * POST /api/play
 * Body (JSON): {"filename": "cane_detected.wav"} 또는 {"filepath": "/audio/..."}
 */
void APIServer::handlePlay(AsyncWebServerRequest* req,
                           uint8_t* data, size_t len,
                           size_t idx,   size_t total) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, (const char*)data, len);

    if (err) {
        req->send(400, CONTENT_JSON, "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String filepath = "";
    if (doc.containsKey("filename")) {
        filepath = String(SD_AUDIO_DIR) + "/" + doc["filename"].as<String>();
    } else if (doc.containsKey("filepath")) {
        filepath = doc["filepath"].as<String>();
    } else {
        req->send(400, CONTENT_JSON, "{\"error\":\"Missing filename field\"}");
        return;
    }

    bool ok = AudioService::play(filepath);
    if (ok) {
        String body = "{\"status\":\"playing\",\"file\":\"" + filepath + "\"}";
        AsyncWebServerResponse* response =
            req->beginResponse(200, CONTENT_JSON, body);
        setupCorsHeaders(response);
        req->send(response);
    } else {
        req->send(500, CONTENT_JSON, "{\"error\":\"Play failed\"}");
    }
}

/**
 * POST /api/stop
 */
void APIServer::handleStop(AsyncWebServerRequest* req) {
    AudioService::stop();
    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, "{\"status\":\"stopped\"}");
    setupCorsHeaders(response);
    req->send(response);
}

/**
 * POST /api/volume
 * Body (JSON): {"volume": 80}
 */
void APIServer::handleVolume(AsyncWebServerRequest* req,
                             uint8_t* data, size_t len,
                             size_t idx,   size_t total) {
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, CONTENT_JSON, "{\"error\":\"Invalid JSON\"}");
        return;
    }
    uint8_t vol = doc["volume"] | 80;
    AudioService::setVolume(vol);
    StorageManager::saveVolume(vol);

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"volume\":%u}", vol);
    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, String(buf));
    setupCorsHeaders(response);
    req->send(response);
}

/**
 * GET /api/rf/sensitivity
 */
void APIServer::handleRfSensitivityGet(AsyncWebServerRequest* req) {
    String body = RFReceiver::getSensitivityJson();
    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, body);
    setupCorsHeaders(response);
    req->send(response);
}

/**
 * POST /api/rf/sensitivity
 * Body (JSON): {"level": 7}  또는  {"threshold_dbm": -72}
 */
void APIServer::handleRfSensitivityPost(AsyncWebServerRequest* req,
                                        uint8_t* data, size_t len,
                                        size_t idx,   size_t total) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, CONTENT_JSON, "{\"error\":\"Invalid JSON\"}");
        return;
    }

    uint8_t regVal = 0;
    if (doc.containsKey("level")) {
        uint8_t level = doc["level"].as<uint8_t>();
        RfSensitivityLevel result = RFReceiver::setSensitivityLevel(level);
        regVal = result.rssiRegVal;
    } else if (doc.containsKey("threshold_dbm")) {
        int8_t dbm = doc["threshold_dbm"].as<int8_t>();
        regVal = RFReceiver::setRssiThresholdDbm(dbm);
    } else {
        req->send(400, CONTENT_JSON,
                  "{\"error\":\"Required: level or threshold_dbm\"}");
        return;
    }

    // NVS에 영구 저장
    StorageManager::saveRssiThreshold(regVal);

    String body = RFReceiver::getSensitivityJson();
    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, body);
    setupCorsHeaders(response);
    req->send(response);
}

/**
 * GET /api/rf/rssi
 * 현재 RF RSSI 값을 즉시 반환
 */
void APIServer::handleRfRssi(AsyncWebServerRequest* req) {
    uint8_t regVal = RFReceiver::readRssiReg();
    int8_t  dbm    = RFReceiver::readRssiDbm();
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"rssi_reg\":%u,\"rssi_dbm\":%d,\"threshold_reg\":%u,"
             "\"threshold_dbm\":%d}",
             regVal, dbm,
             RFReceiver::getCurrentRssiThreshold(),
             -120 + (RFReceiver::getCurrentRssiThreshold() / 2));
    AsyncWebServerResponse* response =
        req->beginResponse(200, CONTENT_JSON, String(buf));
    setupCorsHeaders(response);
    req->send(response);
}
