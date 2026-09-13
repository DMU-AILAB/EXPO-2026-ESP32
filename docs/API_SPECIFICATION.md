# REST API 명세서

## 기본 정보

| 항목 | 값 |
|---|---|
| Base URL | `http://<ESP32_IP>` (기본 포트 80) |
| Content-Type | `application/json` |
| CORS | 허용 (`Access-Control-Allow-Origin: *`) |
| 인증 | 없음 (로컬 네트워크 전용) |

ESP32의 IP 주소는 Wi-Fi 연결 후 라우터 DHCP 목록 또는
`/api/status` 응답의 `wifi_ip` 필드로 확인할 수 있습니다.

---

## 엔드포인트 목록

### 🎥 카메라

---

#### `GET /stream`

실시간 MJPEG 영상 스트림을 반환합니다.

**Response**
- Content-Type: `multipart/x-mixed-replace;boundary=frame`
- 각 프레임은 JPEG 이미지로 전송됩니다.

**예시 (Python)**
```python
import cv2
cap = cv2.VideoCapture("http://192.168.1.100/stream")
while True:
    ret, frame = cap.read()
    if ret:
        cv2.imshow("ESP32-CAM", frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break
```

---

#### `GET /capture`

단일 JPEG 정지 이미지를 반환합니다.

**Response**
- Content-Type: `image/jpeg`
- Content-Disposition: `inline; filename="capture.jpg"`

**예시**
```bash
curl http://192.168.1.100/capture -o frame.jpg
```

---

### 📊 시스템 상태

---

#### `GET /api/status`

시스템 전체 상태를 JSON으로 반환합니다.

**Response 200**
```json
{
  "wifi_ip": "192.168.1.100",
  "wifi_rssi_dbm": -52,
  "free_heap": 180432,
  "uptime_sec": 3600,
  "audio": {
    "state": 0,
    "file": "",
    "volume": 80,
    "ready": true
  },
  "rf": {
    "ready": true,
    "rssi_reg": 40,
    "rssi_dbm": -100,
    "threshold_reg": 90,
    "threshold_dbm": -75,
    "frequency_mhz": 358.5
  },
  "camera": "{\"camera\":\"online\",\"resolution\":\"VGA(640x480)\",\"psram\":true}"
}
```

**`audio.state` 값 정의**

| 값 | 의미 |
|---|---|
| 0 | IDLE (대기 중) |
| 1 | PLAYING (재생 중) |
| 2 | PAUSED (일시 정지) |
| 3 | STOPPING (정지 중) |

---

### 🔊 오디오 제어

---

#### `GET /api/files`

SD 카드 `/audio` 디렉토리 내의 WAV 파일 목록을 반환합니다.

**Response 200**
```json
[
  {"name": "cane_detected.wav", "size": 88244},
  {"name": "remote_guide.wav",  "size": 176488},
  {"name": "startup.wav",       "size": 44100}
]
```

---

#### `POST /api/play`

SD 카드의 WAV 파일을 재생합니다.

**Request Body**
```json
{"filename": "cane_detected.wav"}
```

또는 절대 경로로:
```json
{"filepath": "/audio/cane_detected.wav"}
```

**Response 200**
```json
{"status": "playing", "file": "/audio/cane_detected.wav"}
```

**Response 400** (파라미터 누락)
```json
{"error": "Missing filename field"}
```

**Response 500** (재생 실패)
```json
{"error": "Play failed"}
```

**PC YOLO 연동 예시 (Python)**
```python
import requests

ESP32_IP = "192.168.1.100"

def trigger_audio(filename: str):
    """지팡이 탐지 시 음성 재생 트리거"""
    try:
        r = requests.post(
            f"http://{ESP32_IP}/api/play",
            json={"filename": filename},
            timeout=2
        )
        return r.json()
    except Exception as e:
        print(f"API 호출 실패: {e}")

# 지팡이 탐지 시
trigger_audio("cane_detected.wav")
```

---

#### `POST /api/stop`

현재 재생 중인 음성을 즉시 중지합니다.

**Request Body**: 없음

**Response 200**
```json
{"status": "stopped"}
```

---

#### `POST /api/volume`

볼륨을 설정합니다. 설정값은 NVS에 영구 저장됩니다.

**Request Body**
```json
{"volume": 80}
```

| 파라미터 | 타입 | 범위 | 설명 |
|---|---|---|---|
| `volume` | integer | 0 ~ 100 | 볼륨 (0=무음, 100=최대) |

**Response 200**
```json
{"status": "ok", "volume": 80}
```

---

### 📡 RF 수신 제어

---

#### `GET /api/rf/sensitivity`

현재 RF 수신 감도 설정을 조회합니다.

**Response 200**
```json
{
  "level": 6,
  "threshold_dbm": -75,
  "threshold_reg": "0x5A",
  "frequency_mhz": 358.5
}
```

**감도 레벨 설명**

| Level | threshold_dbm | 특성 |
|---|---|---|
| 1 | -50 dBm | 최저 감도 (매우 근거리, 약 0.5m 이내) |
| 2 | -55 dBm | |
| 3 | -60 dBm | |
| 4 | -65 dBm | |
| 5 | -70 dBm | |
| **6** | **-75 dBm** | **기본값** |
| 7 | -80 dBm | |
| 8 | -85 dBm | |
| 9 | -88 dBm | |
| 10 | -90 dBm | 최고 감도 (원거리, 약 10m 이상) |

---

#### `POST /api/rf/sensitivity`

RF 수신 감도를 설정합니다. 설정값은 NVS에 영구 저장됩니다.

**Request Body (레벨로 설정)**
```json
{"level": 7}
```

**Request Body (dBm으로 직접 설정)**
```json
{"threshold_dbm": -72}
```

**Response 200**
```json
{
  "level": 7,
  "threshold_dbm": -80,
  "threshold_reg": "0x50",
  "frequency_mhz": 358.5
}
```

**Response 400** (파라미터 누락)
```json
{"error": "Required: level or threshold_dbm"}
```

---

#### `GET /api/rf/rssi`

현재 RF 신호 강도(RSSI)를 즉시 읽어 반환합니다.

> [!NOTE]
> 이 엔드포인트는 현재 공간의 358.5MHz 신호 환경을 실시간으로 모니터링할 때 사용합니다.
> 리모컨이 없을 때는 배경 잡음 수준의 낮은 값이 반환됩니다.

**Response 200**
```json
{
  "rssi_reg": 40,
  "rssi_dbm": -100,
  "threshold_reg": 90,
  "threshold_dbm": -75
}
```

---

## PC YOLO 연동 전체 예제

```python
"""
pc_yolo_client.py 사용법:
  pip install opencv-python ultralytics requests
  python examples/pc_yolo_client.py --ip 192.168.1.100
"""
import cv2
import requests
import argparse
import time
from ultralytics import YOLO

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", required=True, help="ESP32 IP 주소")
    parser.add_argument("--model", default="yolov8n.pt", help="YOLO 모델 경로")
    parser.add_argument("--audio", default="cane_detected.wav", help="재생할 음성 파일")
    args = parser.parse_args()

    model     = YOLO(args.model)
    stream_url = f"http://{args.ip}/stream"
    play_url   = f"http://{args.ip}/api/play"

    cap = cv2.VideoCapture(stream_url)
    last_trigger = 0
    COOLDOWN_SEC = 5  # 연속 트리거 방지 (초)

    print(f"[INFO] 스트림 연결 중: {stream_url}")
    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            print("[WARN] 프레임 수신 실패, 재연결 중...")
            time.sleep(1)
            cap = cv2.VideoCapture(stream_url)
            continue

        # YOLO 추론
        results = model(frame, verbose=False)
        cane_detected = False

        for r in results:
            for box in r.boxes:
                # 클래스 이름에 "cane", "stick", "walking_stick" 포함 시 탐지
                cls_name = model.names[int(box.cls[0])].lower()
                if any(kw in cls_name for kw in ["cane", "stick", "walking"]):
                    cane_detected = True
                    # 바운딩 박스 시각화
                    x1, y1, x2, y2 = map(int, box.xyxy[0])
                    cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    cv2.putText(frame, f"{cls_name} {box.conf[0]:.2f}",
                                (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX,
                                0.5, (0, 255, 0), 2)

        # 지팡이 탐지 시 API 호출 (쿨다운 적용)
        now = time.time()
        if cane_detected and (now - last_trigger) > COOLDOWN_SEC:
            try:
                r = requests.post(play_url, json={"filename": args.audio}, timeout=2)
                print(f"[DETECT] 지팡이 탐지! 음성 트리거: {r.status_code}")
                last_trigger = now
            except Exception as e:
                print(f"[ERROR] API 호출 실패: {e}")

        cv2.imshow("ESP32-CAM YOLO", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
```
