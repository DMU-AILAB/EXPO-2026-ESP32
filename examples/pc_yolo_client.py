"""
pc_yolo_client.py - ESP32-CAM MJPEG 스트림에서 시각장애인용 지팡이를 탐지하고
                    ESP32 REST API를 통해 음성 안내를 트리거하는 레퍼런스 구현.

사용법:
    pip install opencv-python ultralytics requests
    python examples/pc_yolo_client.py --ip 192.168.1.100

옵션:
    --ip        ESP32의 IP 주소 (필수)
    --model     YOLO 모델 경로 (기본: yolov8n.pt, 자동 다운로드)
    --audio     탐지 시 재생할 음성 파일명 (기본: cane_detected.wav)
    --cooldown  연속 트리거 방지 쿨다운 초 (기본: 5)
    --conf      YOLO 신뢰도 임계값 (기본: 0.5)
    --show      화면에 결과 표시 여부 (기본: 활성화)
"""

import cv2
import requests
import argparse
import time
import sys
import threading
from queue import Queue, Empty

# ──────────────────────────────────────────────────────────────────────────────
# 설정
# ──────────────────────────────────────────────────────────────────────────────

# 시각장애인 지팡이로 분류될 YOLO 클래스명 키워드
# YOLOv8n.pt (COCO 데이터셋)에는 "umbrella"가 유사하게 탐지될 수 있으나,
# 실제 지팡이 탐지를 위해서는 커스텀 학습 모델 사용을 권장합니다.
CANE_KEYWORDS = ["cane", "stick", "walking_stick", "white_cane", "umbrella"]


def parse_args():
    parser = argparse.ArgumentParser(
        description="ESP32-CAM 시각장애인 지팡이 탐지 YOLO 클라이언트"
    )
    parser.add_argument("--ip",       required=True, help="ESP32 IP 주소")
    parser.add_argument("--model",    default="yolov8n.pt", help="YOLO 모델 경로")
    parser.add_argument("--audio",    default="cane_detected.wav",
                        help="탐지 시 재생할 음성 파일명")
    parser.add_argument("--cooldown", type=float, default=5.0,
                        help="연속 트리거 방지 쿨다운 (초)")
    parser.add_argument("--conf",     type=float, default=0.5,
                        help="YOLO 신뢰도 임계값 (0.0~1.0)")
    parser.add_argument("--no-show",  action="store_true",
                        help="화면 출력 비활성화 (헤드리스 모드)")
    return parser.parse_args()


def trigger_audio(esp32_ip: str, filename: str) -> bool:
    """ESP32 REST API를 통해 음성 파일 재생을 트리거합니다."""
    try:
        response = requests.post(
            f"http://{esp32_ip}/api/play",
            json={"filename": filename},
            timeout=2.0
        )
        if response.status_code == 200:
            data = response.json()
            print(f"[AUDIO] 재생 시작: {data.get('file', filename)}")
            return True
        else:
            print(f"[WARN] API 응답 오류: {response.status_code}")
            return False
    except requests.exceptions.Timeout:
        print("[ERROR] API 호출 타임아웃 (ESP32가 응답하지 않음)")
        return False
    except requests.exceptions.ConnectionError:
        print("[ERROR] ESP32 연결 실패")
        return False
    except Exception as e:
        print(f"[ERROR] API 호출 실패: {e}")
        return False


def get_esp32_status(esp32_ip: str) -> dict:
    """ESP32 시스템 상태를 조회합니다."""
    try:
        r = requests.get(f"http://{esp32_ip}/api/status", timeout=3.0)
        return r.json()
    except Exception:
        return {}


def is_cane_class(class_name: str) -> bool:
    """YOLO 클래스명이 지팡이 관련 키워드를 포함하는지 확인합니다."""
    name_lower = class_name.lower()
    return any(kw in name_lower for kw in CANE_KEYWORDS)


def draw_detections(frame, results, model, conf_threshold: float):
    """탐지 결과를 프레임에 시각화합니다."""
    cane_detected = False

    for r in results:
        for box in r.boxes:
            conf = float(box.conf[0])
            if conf < conf_threshold:
                continue

            cls_id = int(box.cls[0])
            cls_name = model.names[cls_id]
            x1, y1, x2, y2 = map(int, box.xyxy[0])

            if is_cane_class(cls_name):
                # 지팡이: 초록색 박스
                color = (0, 255, 0)
                label = f"CANE: {cls_name} {conf:.2f}"
                cane_detected = True
            else:
                # 기타: 파란색 박스
                color = (255, 100, 0)
                label = f"{cls_name} {conf:.2f}"

            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
            cv2.putText(frame, label, (x1, y1 - 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

    return cane_detected


def api_worker(queue: Queue):
    """별도 스레드에서 API 호출을 비동기로 처리합니다."""
    while True:
        try:
            esp32_ip, filename = queue.get(timeout=1)
            trigger_audio(esp32_ip, filename)
            queue.task_done()
        except Empty:
            continue


def main():
    args = parse_args()

    # ── YOLO 모델 로드 ────────────────────────────────────────────────────────
    try:
        from ultralytics import YOLO
        print(f"[INFO] YOLO 모델 로드 중: {args.model}")
        model = YOLO(args.model)
        print(f"[INFO] 모델 로드 완료 - 클래스 수: {len(model.names)}")
    except ImportError:
        print("[ERROR] ultralytics 패키지가 설치되지 않았습니다.")
        print("        pip install ultralytics")
        sys.exit(1)

    # ── ESP32 상태 확인 ───────────────────────────────────────────────────────
    print(f"[INFO] ESP32 연결 확인 중: {args.ip}")
    status = get_esp32_status(args.ip)
    if status:
        print(f"[INFO] ESP32 연결 성공 - IP: {status.get('wifi_ip', args.ip)}")
        print(f"[INFO] RF 상태: {status.get('rf', {}).get('ready', False)}")
        print(f"[INFO] 카메라: {status.get('camera', 'unknown')}")
    else:
        print("[WARN] ESP32 상태 조회 실패 - 스트림 연결 시도 계속")

    # ── 비동기 API 호출 워커 스레드 시작 ─────────────────────────────────────
    api_queue = Queue(maxsize=3)
    api_thread = threading.Thread(target=api_worker, args=(api_queue,), daemon=True)
    api_thread.start()

    # ── 스트림 연결 ───────────────────────────────────────────────────────────
    stream_url = f"http://{args.ip}/stream"
    print(f"[INFO] MJPEG 스트림 연결 중: {stream_url}")
    cap = cv2.VideoCapture(stream_url)

    if not cap.isOpened():
        print(f"[ERROR] 스트림 연결 실패: {stream_url}")
        print("[INFO] ESP32가 켜져 있고 같은 Wi-Fi에 연결되어 있는지 확인하세요.")
        sys.exit(1)

    print("[INFO] 스트림 연결 성공! 'q' 키로 종료합니다.")

    last_trigger_time = 0.0
    frame_count       = 0
    fps_start_time    = time.time()

    # ── 메인 탐지 루프 ────────────────────────────────────────────────────────
    while True:
        ret, frame = cap.read()
        if not ret:
            print("[WARN] 프레임 수신 실패, 1초 후 재연결 시도...")
            time.sleep(1)
            cap.release()
            cap = cv2.VideoCapture(stream_url)
            continue

        frame_count += 1

        # YOLO 추론 (매 프레임 실행)
        results = model(frame, verbose=False, conf=args.conf)
        cane_detected = draw_detections(frame, results, model, args.conf)

        # 지팡이 탐지 시 음성 트리거 (쿨다운 적용)
        now = time.time()
        if cane_detected and (now - last_trigger_time) > args.cooldown:
            print(f"[DETECT] 지팡이 탐지! 음성 트리거: {args.audio}")
            try:
                api_queue.put_nowait((args.ip, args.audio))
                last_trigger_time = now
            except Exception:
                pass  # 큐 가득 찬 경우 스킵

        # FPS 표시
        elapsed = time.time() - fps_start_time
        if elapsed >= 1.0:
            fps = frame_count / elapsed
            frame_count    = 0
            fps_start_time = time.time()
            cv2.putText(frame, f"FPS: {fps:.1f}", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

        # 상태 표시
        status_text = "CANE DETECTED!" if cane_detected else "Monitoring..."
        color = (0, 255, 0) if cane_detected else (200, 200, 200)
        cv2.putText(frame, status_text, (10, 60),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

        # 화면 출력
        if not args.no_show:
            cv2.imshow("ESP32-CAM | Blind Cane Detector", frame)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                print("[INFO] 사용자가 종료 요청 (q 키)")
                break

    # ── 정리 ──────────────────────────────────────────────────────────────────
    cap.release()
    if not args.no_show:
        cv2.destroyAllWindows()
    print("[INFO] 클라이언트 종료 완료")


if __name__ == "__main__":
    main()
