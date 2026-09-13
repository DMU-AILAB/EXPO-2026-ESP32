# EXPO-2026-ESP32

> **컴퓨터 비전 기반 시각장애인용 스마트 음성유도기 — ESP32-CAM 펌웨어**

## 프로젝트 개요

ESP32-CAM을 핵심으로, PC의 YOLO 비전 모델과 실시간 연동하여 시각장애인용 지팡이를 인식하고 PCM5102A I2S DAC를 통해 고품질 안내 음성을 재생하는 임베디드 시스템입니다. 한국 TTA 표준(358.5000 MHz) 음성유도기 리모컨 수신 기능도 함께 구현되어 있습니다.

## 하드웨어 구성

| 부품 | 기능 |
|---|---|
| ESP32-CAM (AI-Thinker) | 메인 MCU + OV2640 카메라 |
| PCM5102A DAC 모듈 | 16-bit 고품질 오디오 재생 |
| AS4432-SMD (Si4432) | 358.5000 MHz 리모컨 수신 |
| MicroSD 카드 | WAV 음성 파일 저장 |
| 아두이노 우노 | 펌웨어 업로드용 UART 브리지 |

## 빠른 시작

1. **배선**: [docs/WIRING_GUIDE.md](docs/WIRING_GUIDE.md) 참조
2. **Wi-Fi 설정**: `src/Config.h`에서 `WIFI_SSID`, `WIFI_PASSWORD` 수정
3. **SD 카드 준비**: [docs/AUDIO_SD_GUIDE.md](docs/AUDIO_SD_GUIDE.md) 참조
4. **펌웨어 업로드**: PlatformIO → `pio run -t upload` (포트: 아두이노 우노 COM 포트)
5. **PC 클라이언트 실행**:
   ```bash
   pip install opencv-python ultralytics requests
   python examples/pc_yolo_client.py --ip <ESP32_IP>
   ```

## 문서

| 문서 | 내용 |
|---|---|
| [WIRING_GUIDE.md](docs/WIRING_GUIDE.md) | 하드웨어 배선 및 업로드 절차 |
| [API_SPECIFICATION.md](docs/API_SPECIFICATION.md) | REST API 명세 및 연동 예제 |
| [RF_AS4432_GUIDE.md](docs/RF_AS4432_GUIDE.md) | 358.5MHz RF 모듈 기술 가이드 |
| [AUDIO_SD_GUIDE.md](docs/AUDIO_SD_GUIDE.md) | SD 카드 및 WAV 파일 준비 가이드 |

## 주요 API 엔드포인트

| 메서드 | 경로 | 설명 |
|---|---|---|
| GET | `/stream` | MJPEG 실시간 영상 스트림 |
| GET | `/api/status` | 시스템 전체 상태 |
| POST | `/api/play` | 음성 파일 재생 |
| POST | `/api/rf/sensitivity` | RF 수신 감도 설정 |
| GET | `/api/rf/rssi` | 현재 RF RSSI 조회 |

## 라이선스

본 프로젝트는 연구·교육 목적으로 개발되었습니다.