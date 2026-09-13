/**
 * @file CameraService.h
 * @brief OV2640 카메라 제어 및 MJPEG HTTP 스트리밍 서비스
 */
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class CameraService {
public:
    /**
     * @brief OV2640 카메라를 초기화합니다.
     * @return 초기화 성공 시 true
     */
    static bool begin();

    /**
     * @brief AsyncWebServer에 스트리밍/캡처 핸들러를 등록합니다.
     * @param server 핸들러를 등록할 AsyncWebServer 인스턴스
     */
    static void registerRoutes(AsyncWebServer& server);

    /**
     * @brief 현재 카메라 상태 정보를 반환합니다.
     * @return JSON 형식의 상태 정보 문자열
     */
    static String getStatusJson();

    /** @brief 카메라가 정상 초기화되었는지 확인합니다. */
    static bool isReady();

private:
    static bool _ready;

    /** @brief /stream 엔드포인트 핸들러 (MJPEG 멀티파트 스트리밍) */
    static void handleStream(AsyncWebServerRequest* request);

    /** @brief /capture 엔드포인트 핸들러 (단일 JPEG 이미지) */
    static void handleCapture(AsyncWebServerRequest* request);
};
