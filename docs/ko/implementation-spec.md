# 구현 계획과 기술 명세

[English](../implementation-spec.md) | [한국어 README](README.md)

2026-09-06 상태: 아래 설계의 첫 파일 변환 버전을 구현했다. 초기 구현은 CPU 왕복 + 외부 FFmpeg를 사용하며 8/10비트 SDR 입력을 지원한다. 최신 지원 범위와 검증 결과는 [테스트 버전 문서](test-version.md)를 따른다. GPU 전용 연결 등 아래의 모든 장기 설계를 완료했다는 뜻은 아니다.

## 1. 목표와 개발 원칙

목표는 Windows에서 NVIDIA 드라이버의 RTX Video HDR 확장으로 SDR 프레임을 처리하고, 처리 결과의 색공간을 검증한 다음 HEVC Main10 파일로 저장하는 것이다. 브라우저의 외관과 유사한 결과를 얻는 것은 비교 목표이며, 현재 보장되는 동작은 아니다.

최종 제품은 영상 파일 하나를 입력하면 HDR 영상 파일을 저장하는 독립 변환기다. 원본 해상도와 프레임률을 유지하며, 공간 해상도 확대는 현재 범위에 포함하지 않는다. NVEncC 통합은 하지 않는다. 실제 구현 상태와 실행 결과는 `stage1-results.md`에서 관리한다.

우선순위는 **실제 HDR 처리 확인 → 색 해석 확정 → 파일 저장 → 영상 하나를 변환하는 사용 흐름 → 성능 개선**이다. 첫 프로토타입은 CPU 업로드와 읽기를 허용한다. 처음부터 무복사나 수백 줄 이내 구현을 완료 조건으로 두지 않는다.

## 2. 확인된 사실과 검증할 가설

| 항목 | 현재 판단 | 구현에 주는 의미 |
|---|---|---|
| NVIDIA 전용 HDR 확장 호출 | MPC Video Renderer 공개 소스에 구현 존재 | 호출 규약을 참고한 독립 실험 가능 |
| NVIDIA App의 슬라이더가 결과에 반영됨 | 미확인 | 동일 입력과 설정 변화 실험 필요 |
| 화면 표시 없이 offscreen 처리 가능 | 미확인 | 첫 번째 핵심 검증 대상 |
| 성공 HRESULT가 실제 HDR 적용을 뜻함 | 보장되지 않음 | ON/OFF 결과와 반복 변동 비교 필요 |
| R10G10B10A2 출력이 항상 BT.2020/PQ임 | 포맷만으로 확정 불가 | 색공간 설정과 수치 검증 필요 |
| NVENC에 10비트 RGB 포맷이 존재함 | 공개 헤더에서 확인 | 실제 장치 지원과 색 변환은 별도 시험 |
| 드라이버 경로와 SDK가 서로 다른 모델임 | 현재 자료로 증명 불가 | 관측 결과 차이와 내부 구현 차이를 구분 |

MPC의 호출 규약은 GUID `FDD62BB4-620B-4FD7-9AB3-1E59D0D544B3`, version 4, method 3, enable 비트다. 이 소스의 존재가 독립 프로그램에서의 동작이나 드라이버 호환성 보증은 아니다. [MPC 구현](https://raw.githubusercontent.com/Aleksoid1978/VideoRenderer/master/Source/D3D11VP.cpp)

## 3. 범위

### 실험판 v0.1

- Windows x64, C++20, CMake, MSVC, Windows SDK.
- 명시적으로 선택한 NVIDIA D3D11 하드웨어 장치.
- 1920×1080, progressive, BT.709 limited NV12 시험 패턴 및 raw 입력.
- 확장 ON/OFF, 반복 프레임, offscreen 처리, raw 덤프, JSON 진단.
- GUI, 오디오, NVENC, CUDA는 후속 단계이며 NVEncC 통합은 범위 밖이다.
- PNG 입력은 선택 기능이다. sRGB/ICC 처리와 NV12 변환의 불확실성을 줄이기 위해 수치가 정해진 NV12 패턴부터 시작한다.

### 독립 변환기 v0.2

- 첫 지원 입력: SDR BT.709 limited, H.264/HEVC 8비트 4:2:0, progressive, 고정 해상도.
- 출력: HEVC Main10, 10비트 4:2:0, BT.2020/PQ/BT.2020 non-constant-luminance 신호, 우선 MKV.
- 정상 색 변환을 확인한 이후에만 출력에 HDR 태그를 기록한다.
- 오디오 stream copy, 시간축 보존을 지원한다. 대상 컨테이너가 코덱을 수용하지 못하면 명시적으로 실패한다.
- HDR 입력, 인터레이스, 중간 해상도 변경, 색정보가 불명확한 입력은 기본 거부한다. 추후 명시적 확장 대상으로 둔다.

## 4. 단계별 구조

```text
실험판:
정의된 NV12 패턴/raw → D3D11 업로드 → VideoProcessor + NVIDIA 확장
                                         ↓
                              R10G10B10A2 결과
                                         ↓
                             staging 읽기 → raw + JSON

동영상판:
FFmpeg demux/decode (D3D11VA) → D3D11 NV12 텍스처
                                         ↓
                             검증된 HDR 처리 모듈
                                         ↓
                      R10G10B10A2, 색 해석 확정
                       ↙                       ↘
          검증된 GPU RGB→P010 변환       NVENC 직접 RGB 입력 시험
                       ↘                       ↙
                             NVENC HEVC Main10
                                         ↓
                          FFmpeg mux + 오디오 복사
```

동영상 디코드는 D3D11VA를 우선한다. FFmpeg의 CUDA/NVDEC 출력과 D3D11 텍스처는 같은 자원 계약이 아니므로 연결을 자동으로 가정하지 않는다. D3D11VA 프레임의 텍스처와 array slice, 장치와 잠금 계약을 처리한다. [FFmpeg D3D11 하드웨어 컨텍스트](https://ffmpeg.org/doxygen/trunk/hwcontext__d3d11va_8h_source.html)

## 5. 파일 구조와 모듈 계약

아래는 앞으로 만들 구조다. 현재 존재하는 구현을 뜻하지 않는다.

```text
CMakeLists.txt
src/
  app/test_main.cpp              # probe/frame 명령
  app/convert_main.cpp           # 후속 convert 명령
  d3d/device_context.*           # 어댑터 선택, 장치, 진단
  d3d/video_processor.*          # 포맷 검사, 뷰, Blt
  d3d/nvidia_hdr_extension.*     # 확장 ABI를 한 곳에 격리
  d3d/texture_readback.*         # 동기화와 RowPitch 처리
  color/color_contract.*        # primaries/transfer/matrix/range
  input/pattern_generator.*     # 재현 가능한 NV12 패턴
  io/raw_frame_writer.*         # packed raw + sidecar
  diagnostics/run_manifest.*    # 환경, 설정, 결과, 오류
  media/ffmpeg_decoder.*        # 후속 단계
  media/nvenc_encoder.*         # 후속 단계
  media/muxer.*                 # 후속 단계
  shaders/rgb_to_p010.hlsl      # 필요 시 후속 단계
tests/                         # CPU 수치 검증, GPU 통합 시험
tools/                         # 분석 도구
docs/
artifacts/                     # 실행 산출물; Git 제외 예정
```

핵심 논리 API는 다음 역할을 갖는다. 실제 서명은 구현 시 RAII와 오류 타입에 맞춰 정한다.

| API | 입력 → 출력 | 계약 |
|---|---|---|
| `ProbeEnvironment` | 어댑터 선택 → 진단 보고서 | 지원과 미확인을 구분 |
| `CreateProcessor` | 크기·입출력 색 계약 → 처리기 | 정확한 포맷/색공간 조합 검사 |
| `SetHdrEnabled` | bool → 호출 상태 | 실제 적용 판정과 분리 |
| `ProcessFrame` | texture/slice/PTS → 출력 프레임 | 입력 수명 유지, 출력 준비 상태 제공 |
| `ReadbackFrame` | 출력 프레임 → packed bytes | GPU 완료 확인, pitch 제거 |
| `EncodeFrame` | 검증된 프레임 → 패킷 | 완료 전 자원 재사용 금지 |

프레임 계약에는 texture, subresource, width/height, DXGI format, adapter LUID, PTS/timebase, 색정보, 자원 소유권·완료 상태를 포함한다. `R10G10B10A2` 같은 저장 형식을 transfer function과 혼동하지 않는다.

## 6. D3D11 처리 요구사항

1. DXGI로 어댑터를 열거하고 NVIDIA Vendor ID와 사용자가 고른 LUID/index로 선택한다. WARP로 조용히 대체하지 않는다.
2. `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`를 사용한다. Debug layer는 설치 여부를 확인하고 선택적으로 활성화한다.
3. `ID3D11VideoDevice`, `ID3D11VideoContext1`, `ID3D11VideoProcessorEnumerator1` 확보 여부를 기록한다.
4. progressive content descriptor에 해상도, 입력/출력 프레임률을 명시하고 processor를 생성한다.
5. 입력 NV12와 출력 R10G10B10A2의 지원 여부 및 정확한 색공간 변환 조합을 조회한다. 불지원 조합을 무조건 성공으로 처리하지 않는다. [Microsoft 변환 지원 검사](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11videoprocessorenumerator1-checkvideoprocessorformatconversion)
6. 초기 후보는 입력 `DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709`, 출력 `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`이다. 이는 시험 설정이며 출력 의미의 증명이 아니다.
7. source/destination rect는 원본 크기로 고정한다. 자동 보정 및 일반 필터 상태를 명시하고, 업스케일링·VSR은 초기 시험에서 비활성화한다.
8. 확장 payload는 세 개의 32비트 값(version, method, flags)으로 독립 정의하고 크기 12바이트를 정적 검사한다. enable은 flags bit 0, 나머지는 0으로 둔다. 관측된 ABI와 실제 전송 바이트를 기록한다.
9. stream index 0에 확장을 설정하고 각 HRESULT를 저장한다. 이 API는 드라이버 확장 데이터를 전달하는 일반 진입점이다. [Microsoft 확장 API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videocontext-videoprocessorsetstreamextension)
10. 유효한 input/output view와 활성 stream으로 `VideoProcessorBlt`를 호출한다. GPU 완료 후 staging으로 읽는다. `Flush`만으로 완료를 가정하지 않는다.
11. 읽기 시 `RowPitch`를 존중하고 행당 width×4바이트만 덤프한다. ON/OFF는 독립 processor 또는 새 프로세스로 실행해 상태 잔류를 줄인다.

### 덤프 형식

- `.rgb10a2`: little-endian 32비트 packed 픽셀, 행 패딩 없음.
- bit 0–9 R, 10–19 G, 20–29 B, 30–31 A로 읽고 원색 패턴으로 확인한다.
- `.json`: schema version, 크기, 저장 포맷, pitch 정책, 요청 색공간, 검증된 색 해석 또는 unknown, 프레임 번호, PTS, 입력 해시, raw 해시, 확장 payload/HRESULT, 설정·환경 기록.
- 색 해석 미확정 결과에는 nit 통계를 확정값처럼 기록하지 않는다. PQ 가정 분석은 별도 후보 분석으로 표시한다.

## 7. 인코딩과 색 변환

첫 안정 경로는 검증된 PQ RGB를 P010으로 변환해 인코딩하는 방식으로 계획한다. BT.2020/PQ RGB임이 확인됐다면 PQ 재적용이나 BT.709→BT.2020 변환을 반복하지 않는다. 필요한 작업은 비선형 RGB에서 BT.2020 NCL Y′CbCr 행렬 변환, limited-range 양자화, 정의된 4:2:0 chroma 필터링·위치 처리다. P010에서는 10비트 값을 16비트 워드의 상위 비트에 저장하고 낮은 6비트를 0으로 둔다. GPU 구현은 CPU 기준 계산과 비교한다.

직접 RGB 입력은 별도 최적화 분기다. 공개 헤더의 `ABGR10`은 R이 최하위 10비트인 형식이므로 R10G10B10A2와 우선 대조할 후보는 **ABGR10**이다. 이름만 보고 ARGB10으로 연결하지 않는다. 구현에는 실제 채택한 SDK 헤더를 사용한다. [NVIDIA 포맷 정의](https://raw.githubusercontent.com/NVIDIA/video-sdk-samples/master/Samples/NvCodec/NvEncoder/nvEncodeAPI.h)

NVENC 입력 포맷과 Main10 능력은 런타임 조회한다. 외부 D3D11 자원을 등록·매핑하고 완료 후 해제하는 수명 관리를 구현한다. RGB 직접 입력의 내부 행렬·range 결과는 재디코드 검증을 통과해야 한다. VUI 태그만으로 픽셀 변환이 올바르다고 판단하지 않는다. NVIDIA 문서는 RGB 인코딩이 내부적으로 CUDA를 이용하는 기능에 포함됨을 설명하므로 목표는 애플리케이션의 CPU 프레임 왕복 제거로 표현한다. [NVENC 프로그래밍 가이드](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)

HDR 정적 메타데이터는 실제로 아는 값만 넣는다. 디스플레이 피크를 MaxCLL로 복사하지 않는다. MaxCLL/MaxFALL을 계산한다면 사용한 휘도 계산법과 전체 프레임 분석 범위를 기록한다. mastering display 정보가 없다면 임의 생성하지 않고, 생략 여부와 플레이어 호환성을 검증한다.

## 8. 시간축·자원·오류 정책

- 디코더 출력 순서와 PTS를 보존하고 인코더 재정렬 시 DTS를 별도로 관리한다. 유리수 timebase를 사용한다.
- 종료 시 디코더·인코더를 drain하고 남은 패킷과 컨테이너 trailer를 기록한다.
- 초기 처리는 단일 제출 스레드로 단순화한다. 후속 파이프라인은 크기가 제한된 큐와 명시적인 완료 조건을 사용한다.
- 해상도 변경, device removed, map/encode 실패에는 프레임 번호와 원인을 기록하고 변환을 중단한다.
- 임시 출력 파일은 성공 후 최종 이름으로 바꾼다. 기존 파일 덮어쓰기는 명시 옵션으로만 허용한다.
- 실제 적용 불명확 상태를 `verified`로 보고하지 않는다. 진단은 성공할 수 있지만 HDR 검증은 `inconclusive`일 수 있다.

종료 코드 제안: 0=명령 완료, 2=입력/옵션 오류, 3=환경·기능 불지원, 4=GPU/API 실패, 5=검증 실패 또는 미확정, 6=인코딩·mux 실패. 자세한 상태는 JSON에 기록한다.

## 9. 의존성과 배포 준비

v0.1은 Windows SDK 중심으로 구성한다. FFmpeg와 Video Codec SDK는 필요 단계에서 추가하고 버전·다운로드 출처·빌드 옵션을 고정한다. SDK가 오래됐다는 인상이나 원문의 최신 버전 주장은 아키텍처 선택 근거로 삼지 않는다.

MPC 코드를 복사하는 작업은 이 계획에 포함하지 않는다. 확장 규약을 참고한 독립 구현의 출처를 기록한다. 배포 전에는 실제 포함된 코드·헤더·FFmpeg 빌드별 라이선스와 재배포 조건을 검토한다. 독립 작성만으로 모든 라이선스 검토가 끝났다고 단정하지 않는다.
