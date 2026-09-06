# RTX Video HDR 업스케일러

**이 프로젝트의 코드는 OpenAI Codex를 사용해 작성되었습니다.**

**SDR 동영상 하나를 입력하면 NVIDIA RTX Video HDR로 HDR 업스케일링해 영상 파일로 저장하는 Windows 프로그램입니다.**

GUI에서 영상, GPU, 출력 형식과 품질을 선택할 수 있습니다. 결과는 **HEVC Main10 · BT.2020 · PQ** 형식의 MKV 또는 MP4로 저장됩니다. 모든 변환은 로컬 PC에서 실행합니다.

> 현재 버전: **v0.4.0 — GPU 직접 전달**
> 원본 해상도와 프레임률을 유지합니다. 해상도 업스케일링이나 프레임 보간 기능은 포함하지 않습니다.

[사용 기술](#무엇을-이용하나요) · [NGX HDR 비교](#ngx-hdrtruehdr과-무엇이-다른가요) · [작동 과정](#어떤-순서로-작동하나요) · [GUI 사용법](#gui-사용법) · [CLI 사용법](#명령줄-사용법) · [문제 해결](#오류가-발생했을-때) · [빌드](#소스에서-빌드하기)

## 주요 기능

- 파일 선택 또는 드래그 앤 드롭으로 영상 한 개 변환
- 사용할 NVIDIA GPU 선택 — 디코딩, HDR 처리, 인코딩에 같은 GPU 지정
- MKV / MP4 저장 및 비트레이트 / CQ 품질 설정
- 하드웨어 디코딩과 GPU 색 변환
- 진행 상황, 처리 속도, 추정 남은 시간 표시
- 시험 변환, 취소, 완료 후 결과 재생·저장 폴더 열기
- 원본 보존, 기존 결과 덮어쓰기 방지, 오류 로그 저장

## 무엇을 이용하나요?

| 구성 요소 | 역할 |
|---|---|
| **NVIDIA 드라이버의 RTX Video HDR 확장** | SDR 프레임에 HDR 처리를 적용하는 핵심 기능 |
| **Direct3D 11 / Video Processor** | 선택한 GPU에서 영상 프레임을 처리하고 NVIDIA HDR 확장을 호출 |
| **D3D11 compute shader** | HDR 처리된 RGB 10비트 픽셀을 인코딩용 P010 형식으로 변환 |
| **FFmpeg + D3D11VA** | 입력 동영상을 하드웨어 디코딩 |
| **FFmpeg + NVIDIA NVENC** | HEVC Main10 하드웨어 인코딩, 오디오 처리, MKV/MP4 파일 구성 |
| **ffprobe** | 입력 정보와 결과 파일의 코덱·색 정보 확인 |
| **C++20 / C# Windows Forms** | C++ 변환 엔진과 .NET Framework 4.8 기반 GUI |

HDR 처리는 드라이버의 D3D11 확장 경로를 사용합니다. RTX Video SDK의 TrueHDR API를 직접 호출하는 구성은 아닙니다. NVEncC의 처리 단계·버퍼 관리 구조를 참고했으며, 실행할 때 NVEncC를 설치하거나 호출하지 않습니다.

## NGX HDR(TrueHDR)과 무엇이 다른가요?

여기서 **NGX HDR**은 NVEncC의 `--vpp-ngx-truehdr`처럼 **RTX Video SDK의 TrueHDR 기능을 애플리케이션에서 호출하는 방식**을 뜻합니다. NVEncC 문서는 이 옵션을 RTX Video SDK 기반 SDR→HDR 변환으로 설명합니다. [NVEncC 옵션 문서](https://github.com/rigaya/NVEnc/blob/master/NVEncC_Options.en.md#--vpp-ngx-truehdr-param1value1param2value2)

둘 다 SDR 영상을 HDR로 확장하는 목적을 갖습니다. **현재 확인할 수 있는 차이는 기능을 호출하는 경로, 조절 가능한 설정, 프레임을 처리하는 구성입니다.** 서로 다른 AI 모델인지, 어느 쪽 화질이 더 좋은지는 이 프로젝트에서 입증하지 않았습니다.

| 비교 항목 | 이 프로그램: 드라이버 RTX Video HDR 경로 | NGX TrueHDR: NVEncC의 SDK 경로 |
|---|---|---|
| HDR 기능 호출 | D3D11 Video Processor에 NVIDIA 전용 HDR 확장 전달 | RTX Video SDK/NGX 기능을 초기화하고 프레임 처리 호출 |
| HDR 효과 조절 | **NVIDIA App에서 RTX Video HDR의 최대 밝기·중간 회색 밝기·대비·채도 조절 가능.** 이 프로그램에는 별도 효과 조절 UI가 없음. 출력 반영 검증 범위는 아래 참고 | `contrast`, `saturation`, `middlegray`, `maxluminance` 제공 |
| 인코딩 품질 설정 | CQ·비트레이트는 HDR 처리 후 HEVC 압축 품질을 조절 | TrueHDR 효과 파라미터와 인코더 품질 설정을 구분해서 사용 |
| 애플리케이션 구성 | 독립 GUI + C++ 엔진 + FFmpeg 공유 라이브러리 및 외부 도구. 앱에서 TrueHDR SDK를 직접 호출하지 않음 | NVEncC의 영상 필터로 SDK 기능을 호출해 인코딩 흐름에 연결 |
| 파일 저장 | HEVC Main10, MKV/MP4 저장까지 이 프로그램이 처리 | TrueHDR은 처리 필터이며 최종 코덱·컨테이너는 NVEncC 등 호출 프로그램에서 결정 |

NGX 쪽 효과 파라미터와 필터 구성은 [NVEncC 옵션 문서](https://github.com/rigaya/NVEnc/blob/master/NVEncC_Options.en.md#--vpp-ngx-truehdr-param1value1param2value2) 및 [NGX 필터 소스](https://github.com/rigaya/NVEnc/blob/master/NVEncCore/NVEncFilterNGX.cpp)를 기준으로 정리했습니다. 이 프로그램의 드라이버 확장 호출과 픽셀 처리는 [pipeline.cpp](src/pipeline.cpp)에서 확인할 수 있습니다. 비교 확인일은 2026-09-06이며, 외부 프로젝트의 구현은 버전에 따라 달라질 수 있습니다.

**사용할 때 알아둘 점**

- 이 GUI의 **CQ나 Mbps를 바꿔도 HDR의 최대 밝기·중간 밝기·채도를 직접 지정하는 것은 아닙니다.** 출력 압축 품질을 바꾸는 설정입니다.
- **HDR 효과는 NVIDIA App의 RTX Video HDR 설정에서 조절할 수 있습니다.** 최대 밝기(peak brightness), 중간 회색 밝기(middle grey), 대비(contrast), 채도(saturation) 슬라이더를 제공합니다. [NVIDIA 공식 안내](https://www.nvidia.com/en-au/geforce/news/nvidia-app-beta-update-rtx-vsr-hdr-controls-and-more/)
- NVIDIA App의 조절 기능과 이 프로그램의 출력 반영 검증은 구분합니다. 현재 프로그램은 Windows HDR와 드라이버 RTX Video HDR이 작동하는 환경에서 검증했으며, 각 슬라이더 변경이 저장된 HDR 파일에 어떻게 반영되는지는 아직 별도로 측정하지 않았습니다.
- SDK를 직접 호출하지 않는다는 사실만으로 **드라이버 내부에서도 NGX를 사용하지 않는다**고 판단할 수는 없습니다. 내부 모델·알고리즘의 동일성 또는 차이는 확인되지 않았습니다.
- SDK라는 이유로 오래된 모델이라고 보거나, 드라이버 경로라는 이유로 화질·속도가 더 좋다고 단정하지 않습니다. 비교하려면 같은 입력 프레임, GPU·드라이버, HDR 설정, 색 변환 및 인코딩 조건을 맞춰야 합니다. 기존 성능 기록은 이 프로그램 자체의 측정이며 NGX HDR과의 비교 벤치마크가 아닙니다.

또한 **HDR 확장과 해상도 확대는 별개 기능**입니다. NVIDIA는 RTX Video SDK의 Super Resolution과 SDR→HDR 톤 매핑을 구분해 설명합니다. 이 프로그램의 “HDR 업스케일링”은 SDR→HDR 처리를 뜻하며 픽셀 해상도는 유지합니다. [NVIDIA RTX Video SDK 안내](https://developer.nvidia.com/rtx-video-sdk/getting-started)

## 어떤 순서로 작동하나요?

```mermaid
flowchart TD
    A[SDR 동영상 선택] --> B[입력 헤더 확인 · HDR 효과 검사]
    B --> C[D3D11VA 하드웨어 디코딩]
    C --> D[변환 중 프레임 시간축 검사]
    D --> E[D3D11 Video Processor · RTX Video HDR]
    E --> F[GPU에서 RGB 10비트 → P010 변환]
    F --> G[NVENC로 HEVC Main10 인코딩]
    G --> H[오디오 처리 · MKV 또는 MP4 구성]
    H --> I[출력 정보 확인 후 최종 파일 저장]
```

1. **입력과 실행 환경을 확인합니다.** 코덱, 해상도, 색 정보와 프레임률을 읽습니다. 별도의 시험 패턴에 HDR을 끄고 켜서 결과 차이가 있는지도 확인합니다. 차이가 충분하지 않으면 변환을 중단합니다.
2. **실제 변환에 필요한 프레임을 디코딩합니다.** 기본값은 D3D11VA 하드웨어 디코딩입니다. 시작 전에 영상 전체를 다시 디코딩하는 검사는 하지 않습니다.
3. **디코딩한 프레임의 시간축을 검사합니다.** 프레임 번호, 타임스탬프, 크기, 색 정보 등을 순서대로 확인합니다. 가변·불연속 시간축 등 지원하지 않는 입력을 발견하면 중단합니다.
4. **GPU에서 HDR 처리와 색 변환을 수행합니다.** 드라이버 HDR 출력의 RGB 픽셀을 BT.2020/PQ로 해석하고, GPU 셰이더로 limited-range P010에 담습니다. P010은 10비트 YUV 4:2:0 데이터를 담는 픽셀 형식입니다.
5. **인코딩과 저장을 마무리합니다.** NVENC로 영상을 인코딩하고 오디오를 결합합니다. 코덱·색 태그·크기를 확인한 뒤 최종 결과 파일을 만듭니다.

기본 경로는 FFmpeg 공유 라이브러리와 같은 D3D11 장치를 사용합니다. 디코딩 텍스처 → HDR → P010 텍스처 → NVENC로 연결하며, 영상 프레임을 CPU로 읽어오지 않습니다. 8개 NVENC surface와 4프레임 지연으로 처리를 겹칩니다. 오디오 결합과 출력 확인에는 외부 FFmpeg/ffprobe를 사용합니다. `--pipe-video`는 이전의 파이프 처리 경로를 선택합니다. 전체 출력 재디코딩 검사는 기본 실행에서 생략하며 개발용 옵션으로 선택할 수 있습니다.

## 실행에 필요한 환경

- **Windows x64**, **.NET Framework 4.8**
- 이 프로그램의 HDR 효과 검사를 통과하는 **NVIDIA RTX GPU와 드라이버 환경**
- **Windows HDR 및 NVIDIA RTX Video HDR이 작동하는 설정**
- D3D11VA와 `hevc_nvenc`를 지원하는 **ffmpeg.exe**, **ffprobe.exe**
- 결과를 올바르게 감상할 수 있는 **HDR 디스플레이와 HDR 지원 플레이어**

첫 실행의 **필수 구성 설치** 버튼 또는 `Setup-Runtime.cmd`를 실행하면 FFmpeg 공유 DLL과 ffmpeg/ffprobe가 프로그램 옆에 설치됩니다. 인터넷 연결이 필요하며 날짜 고정 URL의 ZIP을 SHA256 검증한 뒤 사용합니다. 설치는 프로그램 폴더 안에서만 이루어지고 관리자 권한이나 PATH 변경은 필요하지 않습니다. CLI에서는 `--ffmpeg-dir`로 도구 폴더를 지정할 수도 있습니다. 프로그램은 Windows HDR나 NVIDIA App 설정을 변경하지 않습니다.

검증 환경은 RTX 5080 / RTX 4060, NVIDIA 드라이버 616.56, FFmpeg 공유 라이브러리 8.1.2 및 외부 도구 8.0/8.1.2입니다. 드라이버 확장 동작은 GPU·드라이버·디스플레이 설정에 영향을 받을 수 있으며, 모든 조합의 호환성을 보장하지 않습니다.

## GUI 사용법

### 1. 프로그램 실행

[GitHub Releases](https://github.com/yoonun-ynun/RTX-Video-HDR-Upscaler/releases/latest)에서 Windows x64 ZIP을 내려받아 압축을 풀고 **`RTXVideoHDR.exe`**를 실행합니다. 소스만 받은 경우에는 아래 [빌드 방법](#소스에서-빌드하기)을 따르세요.

배포 ZIP에는 GUI, 변환 엔진, 기본 설정, 문서와 구성 설치 도구가 포함됩니다. FFmpeg 바이너리를 재배포하지 않으며, 설치 도구가 BtbN의 공식 GitHub 배포에서 직접 내려받습니다. 프로그램 폴더와 PATH에 있는 FFmpeg 도구를 먼저 확인해 그대로 사용합니다. 일반 단일 실행 파일 배포에 GPU 처리용 DLL이 없으면 DLL 추가만 안내합니다. 설치 후 GUI가 다시 열립니다. 원격 파일을 받기 어려운 환경에서는 날짜 고정 ZIP을 따로 받아 `tools/setup-runtime.ps1 -Archive 경로 -Destination 프로그램폴더`로 설치할 수 있습니다. 시험 영상과 개인 설정은 포함하지 않습니다.

다음 파일은 같은 폴더에 보관합니다.

```text
RTXVideoHDR.exe                 GUI
RTXVideoHDR.exe.config          GUI 실행 설정
settings.ini                   출력 품질 설정 — 자동 저장
RTXVideoHDRConvert.exe          실제 변환 엔진
Setup-Runtime.cmd              첫 실행 구성 설치
tools/                         설치 스크립트와 검증 해시
native-runtime.required        GUI의 구성 확인 표시 파일
avcodec-62.dll 등               설치 도구가 받는 공유 라이브러리
ffmpeg.exe / ffprobe.exe        설치 도구가 받는 오디오 결합·검사 도구
```

### 2. 영상과 출력 설정 선택

1. **원본 영상 → 찾아보기**로 파일을 선택합니다. 창에 파일 하나를 끌어놓아도 됩니다.
2. **저장 위치**를 확인합니다. 기본값은 원본 옆의 `원본이름.hdr.mkv`입니다. 영상을 바꾸면 수동 지정했던 경로도 새 원본 옆의 HDR 파일명으로 갱신되며, 선택한 MKV/MP4 형식은 유지됩니다.
3. **사용할 GPU**에서 NVIDIA GPU를 선택합니다. 같은 모델이 여러 개라면 GPU 번호로 구분합니다.
4. **저장 형식**과 **인코딩 방식**을 선택합니다.
5. **HDR 업스케일링 시작**을 누릅니다.

완료 후 **결과 재생**이나 **저장 폴더** 버튼을 사용할 수 있습니다. 작업 중 **취소**를 누르거나 창을 닫으면 현재 변환을 중단합니다. 원본은 변경하지 않으며 임시 파일은 진단을 위해 남습니다.

### 영상 완료 이후의 상태 표시

마지막 프레임을 처리한 뒤에도 인코더 출력, 오디오, 컨테이너 작업이 남을 수 있습니다. GUI는 다음 단계를 구분해 표시합니다.

1. **영상 프레임 처리 완료 · 인코더 마무리 중** — 프레임 처리 막대는 100%로 표시합니다.
2. **영상 완료 · 오디오 muxing 중** — MKV는 오디오 복사, MP4는 AAC 변환과 파일 구성을 진행합니다. 오디오가 없으면 영상 컨테이너만 구성합니다.
3. **영상·오디오 처리 완료 · 결과 검사 중** — 코덱과 HDR 색 정보를 검사합니다.
4. **결과 파일 저장 마무리 중** — 최종 경로에 파일을 저장합니다.
5. **HDR 변환 완료** — 엔진이 성공 종료하고 최종 파일이 존재할 때 표시합니다.

오디오 muxing부터 최종 저장까지는 FPS와 영상 기준 남은 시간을 숨기고 작업 중 표시를 사용합니다. 각 단계 알림은 엔진에서 즉시 전달하므로 이전 FPS 화면이 출력 버퍼에 묶여 남는 문제를 방지합니다.

### 처리 속도(FPS) 표시

- **최근 5초 FPS**: 최근 5초 동안 처리한 프레임 수 ÷ 5초. 시작한 지 5초 미만이면 실제 경과 구간으로 계산합니다.
- **누적 평균 FPS**: 프레임 처리 루프 시작부터 처리한 전체 프레임 수 ÷ 경과 시간.
- **남은 시간**: 추정 잔여 프레임 수를 최근 처리 속도로 나눈 값입니다. 마지막 오디오 처리·파일 마무리 시간은 포함하지 않습니다.

두 수치는 입력 영상의 재생 프레임률이 아닌 변환 처리량입니다. 타이머는 단조 증가 시계를 사용하며, 디코딩 결과 읽기·시간축 검사·HDR 처리·색 변환을 거쳐 인코더에 전달한 프레임을 셉니다. GPU 연산만의 속도나 최종 파일에 기록된 프레임 수를 뜻하지 않습니다. 디코딩·GPU·파이프 대기도 경과 시간에 포함됩니다. 초기 헤더 확인/HDR 효과 검사와 마지막 인코더 종료·오디오 처리·파일 마무리는 실시간 FPS 계산 범위 밖입니다.

이전 버전의 FPS는 누적 평균만 표시했습니다. 예를 들어 초반 100fps 이후 50fps로 일정하게 처리하면 최근 속도는 50fps여도 누적 평균은 한동안 계속 내려갑니다. 따라서 누적 평균만으로 현재 속도가 떨어지고 있다고 판단할 수 없습니다. 처음부터 처리량과 대기 시간이 모두 일정하면 누적 평균이 지속적으로 내려가지는 않습니다.

화면은 처리된 프레임이 나올 때 약 0.5초 간격으로 갱신합니다. 한 프레임 처리가 오래 막히면 다음 프레임이 완료될 때까지 마지막 측정값이 표시됩니다.

### MKV와 MP4의 차이

MKV와 MP4는 파일을 담는 **컨테이너**입니다. 둘 다 영상 코덱은 HEVC Main10이며 HDR 색 형식도 같습니다.

| 설정 | MKV | MP4 |
|---|---|---|
| 영상 | HEVC Main10 / BT.2020 / PQ | HEVC Main10 / BT.2020 / PQ |
| 오디오 | 원본 스트림 복사 | AAC 320 kbps 목표로 재인코딩 |
| 특징 | 오디오 재압축 없이 보존 | hvc1 태그와 faststart 적용 |

### 품질 설정

| 방식 | GUI 설정 | 의미 |
|---|---|---|
| **품질 기준 (CQ)** | 기본 **18**, 범위 0~51 | 낮을수록 높은 품질과 큰 파일을 지향합니다. 장면에 따라 비트레이트가 달라집니다. |
| **평균 비트레이트 (VBR)** | Mbps 단위 입력 | 지정한 평균 비트레이트를 목표로 인코딩합니다. 예: 40 = 40 Mbps |

비트레이트가 너무 낮으면 압축으로 화질이 떨어질 수 있습니다. 다만 높은 값이 원본에 없는 디테일을 복원하지는 않습니다. VBR 값은 고정 비트레이트나 최저 화질 보장이 아니며, 40 Mbps는 사용 예시입니다. CQ와 VBR은 하나를 선택합니다.

### 설정 자동 저장

출력 품질 설정은 실행 파일 옆의 **`settings.ini`**에 변경 즉시 저장되며 다음 실행 때 복원됩니다.

- 인코딩 방식(CQ/VBR), CQ 값, 평균 비트레이트
- 저장 형식(MKV/MP4), 선택한 GPU 번호와 이름

시험 변환과 색 정보 간주 옵션은 입력별 설정으로 다음 실행 때 초기화됩니다. 원본 영상·저장 경로는 설정 파일에 저장하지 않습니다. 저장한 GPU를 찾지 못하면 사용 가능한 첫 GPU를 선택하고, 잘못된 설정값은 기본값으로 대체합니다. 설정을 쓸 수 없는 폴더에서는 GUI 로그에 저장 실패를 표시합니다.

설정을 초기화하려면 프로그램을 종료한 뒤 `settings.ini`를 삭제하세요. 새 버전 폴더로 옮길 때 기존 `settings.ini`를 복사하면 설정을 이어서 사용할 수 있습니다.

### 추가 옵션

- **시험 변환: 첫 432프레임** — 짧은 결과를 먼저 확인합니다. 길이는 입력 프레임률에 따라 달라집니다. 71.928fps에서는 약 6초, 30fps에서는 14.4초입니다.
- **색 정보가 없는 SDR을 BT.709로 간주** — 색 태그가 빠진 확실한 SDR 영상에만 사용합니다. 확인된 다른 색공간이나 HDR 입력을 강제로 변환하는 옵션은 아닙니다.

## 작업 후 권장: HDR 메타데이터를 명시한 최종 재인코딩

**HDR 업스케일링 작업 후에는 결과를 확인하고, 정확한 `master-display`, `colormatrix`, `colorprim`, `transfer`를 명시하여 최종 재인코딩하는 것을 권장합니다.** 특히 최종 배포 파일은 픽셀의 색 표현과 HDR 메타데이터가 일치하는지 확인하세요.

아래 이름은 NVEncC 옵션 기준입니다. 다른 인코더를 사용하면 해당 도구의 동등한 옵션을 지정합니다.

| 항목 | 이 프로그램의 결과를 후처리할 때 |
|---|---|
| `colormatrix` | `bt2020nc` — BT.2020 non-constant luminance |
| `colorprim` | `bt2020` — BT.2020 색 원색 |
| `transfer` | `smpte2084` — PQ 전달 함수 |
| `master-display` | 실제 HDR 마스터링 조건에 맞는 RGB 원색·백색점 좌표와 최대·최소 휘도 |

현재 엔진은 `colormatrix`·`colorprim`·`transfer`에 해당하는 BT.2020/PQ 태그를 이미 기록하고 검사합니다. 다만 **정확한 `master-display` 정보는 자동으로 확정해 기록하지 않습니다.** 마스터링 정보는 SDR 원본이나 파일 확장자만으로 알 수 없으므로, 확인된 조건에 맞춰 지정해야 합니다. 다른 영상의 값이나 임의의 1000nit 예시를 그대로 복사하지 마세요. `colorprim=bt2020`이라고 해서 마스터링 디스플레이의 실제 원색 좌표도 반드시 BT.2020인 것은 아닙니다.

재인코딩할 때는 HEVC Main10과 10비트 출력을 유지하고, 이미 HDR인 결과에 SDR→HDR 처리를 다시 적용하지 마세요. 손실 재인코딩은 추가 압축 손실을 만들 수 있으므로 충분한 품질 설정을 사용하고, 완료 후 색 정보와 실제 재생 결과를 다시 확인하세요. 옵션 형식은 [NVEncC의 HDR 메타데이터 문서](https://github.com/rigaya/NVEnc/blob/master/NVEncC_Options.en.md#--master-display-string-or-copy-hevc-av1)를 참고하세요.

## 지원 입력과 현재 범위

| 항목 | 지원 범위 |
|---|---|
| 입력 영상 코덱 | H.264 / HEVC |
| 입력 색 정보 | SDR BT.709, limited/TV range |
| 입력 픽셀 형식 | 8/10비트 YUV 4:2:0 |
| 프레임 구조 | Progressive, 일정한 프레임률, 정사각형 픽셀 |
| 프레임률 | 1~120fps |
| 해상도 | 가로·세로 짝수, 최대 8192×8192 — 실제 처리 가능 여부는 GPU에 따름 |
| 출력 | HEVC Main10 HDR, MKV / MP4 |

가변 프레임률, 불연속 타임스탬프, 인터레이스, 회전 정보가 있는 입력 등은 현재 버전에서 지원하지 않습니다. 자막·챕터·첨부 파일은 복사하지 않습니다. 한 번에 영상 하나를 처리하며 배치 변환 기능은 없습니다.

브라우저 RTX Video HDR과의 외관·수치적 동일성 및 전체 23분 영상의 장시간 완주는 아직 검증하지 않았습니다. HDR ON/OFF 차이 검사는 효과 작동 여부를 확인하기 위한 것으로, 브라우저 결과와의 동일성 검사는 아닙니다.

## 명령줄 사용법

다음 예시는 실행 파일이 있는 폴더에서 PowerShell로 실행합니다. `input.mp4`를 실제 입력 경로로 바꾸세요.

```powershell
# 사용 가능한 NVIDIA GPU와 번호 확인
.\RTXVideoHDRConvert.exe --list-gpus

# 기본 품질 CQ 18, MKV 출력
.\RTXVideoHDRConvert.exe "input.mp4" --output "output.hdr.mkv"

# GPU 1 선택, 평균 40 Mbps, MP4 출력
.\RTXVideoHDRConvert.exe "input.mp4" --adapter 1 --bitrate 40M --output "output.hdr.mp4"

# CQ를 직접 지정하고 첫 432프레임만 시험
.\RTXVideoHDRConvert.exe "input.mp4" --cq 18 --max-frames 432 --output "preview.hdr.mkv"

# FFmpeg 도구 위치 직접 지정
.\RTXVideoHDRConvert.exe "input.mp4" --ffmpeg-dir "C:\ffmpeg\bin" --output "output.hdr.mkv"
```

CLI의 `--bitrate 40M`과 `--bitrate 40000k`는 같은 값입니다. `--bitrate`와 `--cq`는 함께 지정할 수 없습니다.

| 진단 옵션 | 용도 |
|---|---|
| `--assume-bt709` | 색 태그가 없는 SDR을 BT.709로 간주 |
| `--software-decode` | 하드웨어 대신 소프트웨어 디코딩을 명시적으로 선택 |
| `--cpu-color` | GPU 색 변환 대신 CPU 기준 계산 사용 — 성능 비교용 |
| `--diagnostics` | HDR 효과 비교와 일부 프레임의 raw 표본 저장 |
| `--verify-full` | 출력 전체를 다시 디코딩하여 프레임 수 검사 — 시간이 추가로 필요 |

## 오류가 발생했을 때

GUI 하단의 로그와 출력 폴더의 **`rtxhdr-run-*`** 디렉터리를 확인하세요.

| 파일 | 확인할 내용 |
|---|---|
| `error.json` | 엔진이 보고한 실패 원인 |
| `decode.log` | 입력 디코딩과 프레임 시간축 |
| `encode.log` | NVENC 인코딩 오류 |
| `mux.log` | 오디오 처리와 파일 구성 오류 |
| `environment.json` | 선택한 GPU와 HDR 확장 상태 |
| `result.json` | 성공한 변환의 설정, 프레임 수, 처리 시간 |

- **출력 파일이 이미 있음**: 다른 저장 이름을 선택합니다.
- **HDR 효과가 검출되지 않음**: Windows HDR, NVIDIA RTX Video HDR 설정과 선택한 GPU를 확인합니다.
- **FFmpeg 도구를 찾지 못함**: 두 실행 파일을 엔진 옆이나 PATH에 준비합니다.
- **시간축 불연속 오류**: 현재 지원 범위를 벗어난 입력입니다. 오류에 표시된 프레임과 `decode.log`를 확인합니다.

취소 시에는 프로세스가 즉시 종료되므로 `error.json`이 없을 수 있습니다. 임시 폴더에는 중간 영상이 남아 용량을 사용할 수 있으며, 작업이 종료된 뒤 필요 없는 폴더를 직접 정리할 수 있습니다.

## 소스에서 빌드하기

필요한 개발 도구:

- Visual Studio 2022의 C++ 데스크톱 개발 도구 및 Windows SDK
- CMake 3.24 이상 또는 Visual Studio에 포함된 CMake
- PowerShell 7
- .NET Framework 4.8 환경 — GUI 빌드는 Windows의 C# 컴파일러 사용

프로젝트 루트에서 실행합니다.

```powershell
# GPU 직접 전달 빌드: tools/ffmpeg-native-lock.json의 패키지를 내려받아 압축 해제
.\tools\build.ps1 -FFmpegRoot "C:\dev\ffmpeg-shared"

# 공유 라이브러리 없이 기존 파이프 경로만 빌드
.\tools\build.ps1

# GUI만 다시 빌드
.\tools\build-gui.ps1

# 빌드된 GUI 실행
.\build\Release\RTXVideoHDR.exe
```

### 코드 구조

| 경로 | 역할 |
|---|---|
| [src/gui.cs](src/gui.cs) | GUI, GPU·품질 설정, 진행 상황, 취소 |
| [src/convert_main.cpp](src/convert_main.cpp) | 입력 확인부터 인코딩·오디오 처리·완료 검증까지의 변환 흐름 |
| [src/pipeline.cpp](src/pipeline.cpp) | D3D11 자원, NVIDIA HDR 확장, GPU P010 변환 |
| [src/frame_timing.h](src/frame_timing.h) | 실제 디코딩 중 프레임 메타데이터와 시간축 검사 |
| [src/child_process.cpp](src/child_process.cpp) | FFmpeg/ffprobe 실행, 파이프, 자식 프로세스 정리 |
| [src/color.h](src/color.h) | GPU 색 변환과 비교하는 CPU 기준 계산 |
| [tests](tests) · [tools](tools) | 빌드, 배포, GUI·색 변환·파일 경로·출력 검증 |

## 검증 기록과 기술 문서

GUI의 MKV/VBR 및 MP4/CQ 변환, GPU 선택, 진행 표시와 취소를 시험했습니다. 별도 검증에서는 HDR 태그, 출력 프레임 수·시간축, 색 변환 정확도, MKV 오디오 보존을 확인했습니다.

v0.2의 4K 432프레임 시험에서 약 36.6~39.2fps를 측정했습니다. 이 수치는 해당 버전과 시험 구간의 결과이며, v0.3 및 모든 입력의 처리 속도를 보장하지 않습니다.

- [v0.3 GUI 사용법·검증](docs/gui-v0.3.md)
- [v0.2 성능 개선·측정과 NVEncC 참고 내용](docs/performance-v0.2.md)
- [긴 파일 경로 오류 수정](docs/path-fix.md)
- [구현 계획과 기술 명세](docs/implementation-spec.md)
- [검증 계획](docs/validation-plan.md)
- [초기 단계 실험 결과](docs/stage1-results.md)

초기 계획·실험 문서에는 당시의 구현 범위와 향후 계획이 포함됩니다. 현재 사용법과 지원 범위는 이 README 및 v0.3 문서를 기준으로 확인하세요.

### 인코더 종료 오류 진단 (로컬 보강 빌드)

변환이 실패하면 GUI에 표시된 진단 폴더를 확인하세요. `error.json`의 `exit_code`는 변환 프로그램의 종료 코드이며, FFmpeg 자체 종료 코드는 아래 파일에 별도로 기록합니다.

- `encode.log.command.json`: 실행한 FFmpeg 경로와 인자 배열. 다른 자식 프로세스도 각 로그 옆에 같은 형식으로 기록합니다.
- `encode.log.failure.json`: 오류 동작, Win32 오류, PID, 프로세스 종료 여부, 종료 코드(10진수·16진수), stdin에 전송한 바이트 수. 아직 실행 중이거나 조회하지 못한 종료 코드는 `null`입니다.
- `encode.log.tail.txt`: 실패 시점의 stderr 끝부분(최대 16 KiB). 로그가 없으면 빈 파일입니다. JSON의 `stderr_tail_hex`에는 같은 내용을 원시 바이트의 16진수로 보존합니다.
- 인코더 입력 전송 실패 시 `error.json` 메시지에는 완전히 전달한 프레임 수, 실패한 프레임 인덱스(0부터 시작), 처리 경과 시간이 포함됩니다. 전달한 프레임 수는 최종 인코딩 완료 프레임 수를 의미하지 않습니다.

파이프 쓰기 실패 시 자식 프로세스 종료를 최대 2초 기다린 뒤, 정리 과정에서 프로세스를 종료하기 전에 상태를 수집합니다. 진단 저장 자체가 실패해도 원래 오류를 유지합니다. 이 보강은 원인 파악을 위한 것으로 인코더 충돌을 자동 복구하거나 변환을 이어서 재개하지는 않습니다. 실행 인자에는 로컬 영상 경로가 포함되므로 로그를 공유할 때 참고하세요.

### GPU 직접 전달과 처리 겹치기

기본 경로는 GPU 텍스처를 직접 전달합니다. `--pipe-video` 경로에서도 입력 읽기·GPU HDR 처리·인코더 전송을 겹쳐 수행합니다. 화질 설정은 유지합니다. CLI의 `--serial-pipeline`으로 기존 순차 방식과 비교할 수 있습니다. `--diagnostics`와 `--cpu-color`는 순차 경로를 사용합니다. 측정 조건과 제한은 [로컬 성능 검증](docs/performance-overlap.md)을 참고하세요.


### v0.4.0 성능과 진단

4K 432프레임·RTX 4060·CQ 18 시험에서 GPU 직접 전달은 약 96.7fps, 이전 순차 경로는 약 22.9fps였습니다. 짧은 구간과 당시 부하에서의 측정이며 다른 영상·GPU의 속도를 보장하지 않습니다. 기존 결과와 전체 프레임 픽셀·타임스탬프·HDR 태그·오디오 일치를 검증했습니다. 인코딩 프리셋, CQ와 HDR 수식은 유지합니다.

직접 경로에서는 `native_gpu_pipeline: true`가 `result.json`에 기록됩니다. `native.log`에는 FFmpeg API 메시지와 주기적인 전달 프레임 수, `native-config.json`에는 라이브러리 버전·입력·인코딩 설정이 남습니다. 파이프 경로의 `encode.log`와 프로세스 종료 진단도 유지합니다. `--serial-pipeline`, `--software-decode`, `--cpu-color`, `--diagnostics`는 비교용 파이프 경로를 사용합니다.

FFmpeg 공유 라이브러리는 LGPL에 따라 사용하며 원래 이름의 DLL을 교체할 수 있습니다. 설치되는 패키지의 라이선스는 `FFmpeg-LICENSE.txt`, 출처·버전·검증 해시는 `runtime-version.json`에서 확인할 수 있습니다. [FFmpeg 소스](https://ffmpeg.org/download.html#get-sources), [BtbN 빌드 스크립트와 배포](https://github.com/BtbN/FFmpeg-Builds)를 참고하세요.
