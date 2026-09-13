# SD 카드 음성 파일 가이드

## 목차
1. [SD 카드 준비](#sd-카드-준비)
2. [디렉토리 구조](#디렉토리-구조)
3. [음성 파일 규격](#음성-파일-규격)
4. [FFmpeg으로 음성 변환](#ffmpeg으로-음성-변환)
5. [권장 음성 파일 목록](#권장-음성-파일-목록)
6. [파일 관리 API](#파일-관리-api)

---

## SD 카드 준비

### 포맷 요구사항

| 항목 | 권장값 |
|---|---|
| 파일 시스템 | **FAT32** (exFAT 미지원) |
| 용량 | **32GB 이하** (ESP32 SD 라이브러리 제한) |
| 클래스 | Class 10 이상 권장 (빠른 읽기 속도) |
| 파티션 | MBR (GPT 미지원) |

### Windows에서 FAT32 포맷

32GB 초과 카드의 경우 Windows 기본 포맷 도구는 FAT32를 지원하지 않습니다.
이 경우 **GUIFormat** (guiformat.exe) 또는 다음 명령어를 사용하세요:

```powershell
# 관리자 권한 PowerShell에서 실행 (E: = SD 카드 드라이브)
format E: /FS:FAT32 /Q /V:BEACON_SD
```

### macOS에서 포맷

```bash
# /dev/disk2 = SD 카드 (diskutil list로 확인)
diskutil eraseDisk FAT32 BEACON_SD MBRFormat /dev/disk2
```

---

## 디렉토리 구조

SD 카드의 루트에 다음 구조로 파일을 배치하세요:

```
/                           ← SD 카드 루트
└── audio/                  ← 음성 파일 디렉토리 (자동 생성)
    ├── startup.wav         ← 시스템 시작 알림 음성
    ├── remote_guide.wav    ← RF 리모컨 수신 시 재생 (기본)
    ├── cane_detected.wav   ← PC YOLO 지팡이 탐지 시 재생
    ├── guide_01.wav        ← 사용자 정의 유도 음성 1
    ├── guide_02.wav        ← 사용자 정의 유도 음성 2
    └── ...                 ← 추가 음성 파일 (n개)
```

> [!IMPORTANT]
> **파일명 규칙**:
> - 영문자, 숫자, 언더스코어(`_`), 하이픈(`-`), 점(`.`)만 사용
> - 한글 파일명 **사용 금지** (FAT32/ESP32 인코딩 문제)
> - 파일명은 8.3 형식(8자 이름 + 3자 확장자) 또는 LFN(긴 파일명) 모두 지원
> - 확장자는 반드시 `.wav` (소문자 또는 대문자)

---

## 음성 파일 규격

PCM5102A I2S DAC가 지원하는 최적 WAV 파일 형식:

| 항목 | 권장값 | 지원 범위 |
|---|---|---|
| 파일 형식 | RIFF WAV (PCM) | RIFF/PCM 전용 |
| 샘플 레이트 | **44100 Hz** | 8000 ~ 44100 Hz |
| 비트 깊이 | **16-bit** | 16-bit 전용 |
| 채널 수 | **모노 또는 스테레오** | 1~2채널 |
| 파일 크기 | 5MB 이하 권장 | SD 용량까지 |

> [!NOTE]
> **모노 WAV 파일도 자동으로 스테레오로 변환**됩니다.
> PCM5102A는 스테레오 입력만 지원하므로, 펌웨어에서 모노 샘플을
> 좌우 채널에 동일하게 복사하여 전송합니다.

> [!WARNING]
> **지원하지 않는 형식**:
> - MP3, AAC, OGG, FLAC, WMA → WAV로 변환 필요
> - 32-bit float WAV → 16-bit PCM WAV로 변환 필요
> - 압축된 WAV (ADPCM, µ-law 등) → PCM WAV로 변환 필요

---

## FFmpeg으로 음성 변환

[FFmpeg](https://ffmpeg.org/download.html)을 사용하여 다양한 형식을 최적 WAV로 변환합니다.

### FFmpeg 설치

**Windows (winget)**:
```powershell
winget install ffmpeg
```

**macOS (Homebrew)**:
```bash
brew install ffmpeg
```

### 변환 명령어

**MP3 → 16-bit PCM WAV 44100Hz 모노 (용량 절약)**:
```bash
ffmpeg -i input.mp3 -ar 44100 -ac 1 -sample_fmt s16 output.wav
```

**MP3 → 16-bit PCM WAV 44100Hz 스테레오 (고품질)**:
```bash
ffmpeg -i input.mp3 -ar 44100 -ac 2 -sample_fmt s16 output.wav
```

**볼륨 조절 (2배 증폭)**:
```bash
ffmpeg -i input.wav -filter:a "volume=2.0" -ar 44100 -ac 1 -sample_fmt s16 output.wav
```

**무음 구간 앞뒤에 추가 (0.3초)**:
```bash
ffmpeg -i input.wav -filter_complex "[0]adelay=300|300[a];[a]apad=pad_dur=0.3" \
       -ar 44100 -ac 1 -sample_fmt s16 output.wav
```

**TTS로 음성 생성 (Python gTTS 사용)**:
```bash
pip install gtts
python -c "
from gtts import gTTS
texts = {
    'startup': '음성유도기가 시작되었습니다.',
    'remote_guide': '안내 방향으로 이동하십시오.',
    'cane_detected': '시각장애인이 감지되었습니다. 주의하시기 바랍니다.',
    'guide_01': '출입구는 정면입니다.',
    'guide_02': '계단이 있습니다. 주의하십시오.',
}
for name, text in texts.items():
    tts = gTTS(text=text, lang='ko', slow=False)
    tts.save(f'{name}_tmp.mp3')
    print(f'생성 완료: {name}_tmp.mp3')
print('FFmpeg으로 WAV 변환을 진행하세요.')
"
# 각 mp3를 wav로 변환
for f in *_tmp.mp3; do
    ffmpeg -i "$f" -ar 44100 -ac 1 -sample_fmt s16 "${f/_tmp.mp3/.wav}"
done
```

### 일괄 변환 스크립트 (Windows PowerShell)

```powershell
# 현재 디렉토리의 모든 MP3 파일을 WAV로 변환
Get-ChildItem -Filter "*.mp3" | ForEach-Object {
    $outFile = [System.IO.Path]::ChangeExtension($_.FullName, ".wav")
    & ffmpeg -i $_.FullName -ar 44100 -ac 1 -sample_fmt s16 $outFile
    Write-Host "변환 완료: $($_.Name) -> $(Split-Path $outFile -Leaf)"
}
```

---

## 권장 음성 파일 목록

시각장애인 음성유도기에 적합한 안내 음성 내용 가이드입니다:

| 파일명 | 내용 예시 | 트리거 상황 |
|---|---|---|
| `startup.wav` | "음성유도기가 정상 작동합니다." | 전원 켜짐 |
| `remote_guide.wav` | "안내 방향으로 이동하십시오." | RF 리모컨 수신 |
| `cane_detected.wav` | "시각장애인이 감지되었습니다." | YOLO 지팡이 탐지 |
| `guide_entrance.wav` | "출입구는 정면 방향입니다." | 입구 안내 |
| `guide_stairs.wav` | "계단이 있습니다. 주의하십시오." | 계단 경보 |
| `guide_elevator.wav` | "엘리베이터는 우측입니다." | 엘리베이터 안내 |
| `guide_door.wav` | "문이 열립니다." | 자동문 연동 |

> [!TIP]
> **음성 품질 팁**:
> - 배경 잡음 없는 조용한 환경에서 녹음
> - 명확하고 천천히 발음
> - 볼륨은 재생 시 클리핑(찌그러짐)이 없을 정도로 설정
> - `/api/volume` API로 ESP32 소프트웨어 볼륨을 조절하여 최종 출력 조정

---

## 파일 관리 API

### 현재 파일 목록 조회

```bash
curl http://192.168.1.100/api/files
```

응답:
```json
[
  {"name": "cane_detected.wav", "size": 88244},
  {"name": "remote_guide.wav",  "size": 176488},
  {"name": "startup.wav",       "size": 44100}
]
```

### 특정 파일 재생 테스트

```bash
curl -X POST http://192.168.1.100/api/play \
     -H "Content-Type: application/json" \
     -d '{"filename": "startup.wav"}'
```

### RF 리모컨 수신 시 재생 파일 변경

현재는 `Config.h`의 `SD_REMOTE_AUDIO_FILE` 또는 NVS에 저장된 파일을 사용합니다.
향후 대시보드 API를 통해 `/api/rf/audio` 엔드포인트로 변경 예정입니다.

현재 임시 방법 (NVS 직접 설정 없이):
```bash
# startup.wav를 remote_guide.wav로 복사 후 재생
# SD 카드를 PC에 꺼내서 파일 수정 후 재삽입
```
