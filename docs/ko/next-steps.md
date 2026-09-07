# 다음 작업 순서

[English](../next-steps.md) | [한국어 README](README.md)

현재 독립 파일 변환기의 첫 테스트 버전을 구현했다. 최신 동작·검증은 [테스트 버전 결과](test-version.md)와 README를 따른다. 아래는 초기 단계 계획의 기록이며 이미 완료된 작업을 다시 시작하라는 지시가 아니다. 다음 제품 작업은 실제 HDR 외관 피드백을 반영하고 GPU 경로로 속도를 개선하는 것이다.

## 1. 실행 순서

| 순서 | 해야 할 일 | 산출물 | 다음 단계 조건 |
|---|---|---|---|
| 1 | GPU·Windows·드라이버·도구 조사 | 환경 보고서 | 실제 시험 환경 확인 |
| 2 | CMake C++20 x64 프로젝트와 probe 구현 | 실행 파일·지원 능력 JSON | G0 |
| 3 | NV12 패턴 → VP OFF → raw 덤프 | 입력·출력·sidecar | G1 |
| 4 | NVIDIA 확장 ON/OFF와 반복 시험 | 3회 비교 보고서 | G2 |
| 5 | 설정·HDR·offscreen 의존성 조사 | 동작 조건 표 | 제약 재현 가능 |
| 6 | 색공간·캡처 교정 및 비교 | 색 계약, waveform, 비교 보고서 | G3; 브라우저 동일성은 별도 판정 |
| 7 | 짧은 raw 시퀀스 → P010 → NVENC 저장 | 10초 HEVC Main10 파일 | G4 |
| 8 | FFmpeg D3D11VA 디코드 연결 | SDR 영상 변환 | 원본 프레임·시간축 보존 |
| 9 | mux와 오디오 복사·drain 구현 | 재생 가능한 MKV | G5 |
| 10 | 직접 RGB 입력·GPU 큐 최적화 | 성능 및 품질 비교 | G6 |
| 11 | 배포·회귀 시험 정리 | 의존성 목록·사용법·검증 환경 | 독립 도구 재현 가능 |
| 12 | 영상 하나 입력 → HDR 파일 저장 흐름 마무리 | 간단한 실행 방식·진행률·오류 안내 | 독립 변환기 완성 |

7번은 색이 검증된 짧은 raw 시퀀스로 인코더만 시험한다. 이 순서는 디코드 문제와 인코딩 문제를 분리하기 위한 선택이다. 구현 규모보다 게이트 통과를 기준으로 다음 작업을 정한다.

## 2. 지금 바로 시작할 범위

첫 코딩 작업 **1~3번**은 SDR 기준 경로까지 완료했다. 이어서 4번을 별도 변경으로 추가한다. 아래 첫 지시문은 초기 범위 기록이며, 계속 진행할 때는 4절의 지시문과 실제 실행 결과를 함께 사용한다.

아래는 전체 목표 명령 인터페이스다. 현재 실행 가능한 SDR 기준 명령은 README를 참고한다. `--hdr on`, raw 입력, 변환기 명령은 아직 구현되지 않았다.

```powershell
RTXVideoHDRTest.exe probe --adapter 0 --report artifacts/probe.json
RTXVideoHDRTest.exe frame --pattern gray-ramp --size 1920x1080 --hdr off --frames 120 --out artifacts/off
RTXVideoHDRTest.exe frame --pattern gray-ramp --size 1920x1080 --hdr on --frames 120 --out artifacts/on
RTXVideoHDRTest.exe frame --input input.nv12 --size 1920x1080 --input-color bt709-limited --hdr on --out artifacts/raw
# 후속 변환기
RTXVideoHDRConvert.exe --input input.mp4 --output output.mkv --encoder-input p010 --audio copy
```

adapter 0은 예시다. 실제 probe 결과에서 NVIDIA 어댑터를 선택한다. 원본 입력을 덮어쓰지 않고, 결과 파일에는 실행별 경로를 사용한다.

## 3. 코딩 에이전트에게 전달할 첫 지시문

```text
이 저장소의 README.md, docs/implementation-spec.md,
docs/validation-plan.md, docs/next-steps.md를 읽고 첫 구현 단계를 진행해줘.

범위:
1. 실제 GPU, Windows, NVIDIA 드라이버, MSVC, CMake, Windows SDK를 조사한다.
2. C++20/CMake x64의 RTXVideoHDRTest 프로젝트를 만든다.
3. probe 명령으로 NVIDIA 어댑터 선택, 필수 D3D11 video 인터페이스,
   NV12 입력/R10G10B10A2 출력과 지정 색공간 조합 지원을 JSON에 기록한다.
4. 수치가 정의된 BT.709 limited NV12 패턴을 생성한다.
5. HDR 확장 OFF 상태의 VideoProcessorBlt와 raw/JSON 저장을 구현한다.
6. RowPitch, packed RGB10 채널 순서, 파일 크기를 검증한다.
7. 가능한 실제 빌드·GPU 시험을 수행하고 명령과 결과를 기록한다.

지금은 NVIDIA 확장 ON, FFmpeg, NVENC, GUI, NVEncC 통합을 추가하지 않는다.
기능 불지원이나 GPU 실행 불가를 성공으로 보고하지 않는다.
프로그램 설치가 필요하면 먼저 현재 설치 상태와 필요한 구성요소를 정리한다.
결과는 변경 파일, 빌드/실행 결과, G0/G1 판정, 남은 장애 요인으로 보고한다.
```

## 4. 첫 단계 완료 뒤 지시문

```text
기존 RTXVideoHDRTest에 문서의 NVIDIA HDR 확장 어댑터를 추가해줘.
공개 호출 규약을 참고해 독립적으로 작성하고 ABI/payload/HRESULT를 기록해줘.
동일 색공간 조건에서 OFF/ON을 독립 실행하고, 여러 패턴을 120프레임씩 처리해줘.
새 프로세스 3회 반복, 코드값 통계, 재현성, G2 판정을 보고해줘.
S_OK만으로 실제 HDR 적용을 선언하지 말고, 미확정이면 문서의 실패 분기를 따라줘.
G3가 통과하기 전에는 HDR 파일 인코딩을 추가하지 마.
```

## 5. 최종 사용 방식

사용자는 영상 파일 하나를 지정하고 결과 경로를 선택한다. 프로그램은 지원 입력인지 확인한 뒤 원본 해상도·프레임률과 오디오를 유지하며 HDR 파일을 저장한다. 기본 출력 이름은 원본 이름에 `.hdr`를 추가하고 원본을 덮어쓰지 않는다.

먼저 명령줄 변환을 완성하고 파일 끌어놓기용 런처 또는 간단한 파일 선택 UI를 덧붙인다. 진행률·예상 남은 시간·완료 경로를 표시하고, 변환 불가능한 입력은 이유를 안내한다.

NVEncC 통합은 수행하지 않는다. NVENC는 별도의 NVIDIA 하드웨어 인코딩 API이므로 파일 저장 단계에서 계속 사용한다. 현재 “업스케일링”은 SDR→HDR 변환으로 해석하며, 1080p→4K 같은 해상도 확대는 추가 요구가 있을 때 설계한다.
