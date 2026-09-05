# 첫 구현 단계 실행 결과

## 상태

실행일: 2026-09-05. 사용자 요청에 따라 NVEncC 통합을 범위에서 제외했다. 최종 목표는 영상 한 개를 받아 원본 해상도로 HDR 영상 파일을 저장하는 독립 변환기다.

**C++ 프로그램 빌드와 SDR 기준 프레임 처리·저장 검증을 완료했다. HDR 확장, 실제 영상 입력, NVENC 인코딩은 아직 구현하지 않았다.**

## 실제 환경

| 항목 | 확인값 |
|---|---|
| GPU | NVIDIA GeForce RTX 5080, RTX 4060 |
| 시험 어댑터 | DXGI index 0, RTX 5080, LUID low 79784 / high 0 |
| 드라이버 | 616.56 |
| Windows 빌드 | 26200.8655 |
| Visual Studio | Community 2022 17.13.4 |
| MSVC | 19.43.34809.0 |
| Windows SDK | 10.0.22621.0 |
| CMake | Visual Studio 번들 3.30.5 |

DXGI 열거에서 RTX 5080 이름이 index 0과 2에 나타났다. 물리 GPU 수를 이 이름의 개수로 판단하지 않았고 시험에는 index 0만 사용했다. NVIDIA App 설정·현재 모니터 HDR 상태는 이번 단계에서 조사하지 않았다.

첫 CMake 실행은 자식 프로세스 환경에 `Path`/`PATH`가 중복되어 MSBuild가 컴파일러를 실행하지 못했다. 빌드 스크립트에서 자식 환경의 키를 정규화해 해결했다. 시스템 환경이나 Visual Studio 설치는 변경하지 않았다.

## 구현한 내용

- `probe`: 어댑터 열거·선택, D3D11 video 인터페이스, 포맷·색공간 조합 지원 조회, 가능한 경우 processor·input/output view 생성.
- `frame`: NV12 gray-ramp/color-bars 생성, GPU 업로드, VideoProcessorBlt, 완료 query 대기, staging 읽기.
- little-endian packed RGB10A2 파일, sidecar JSON, 전 프레임 RGB 최솟값·최댓값 CSV.
- 첫·마지막 raw만 저장해 디스크 사용량 제한. 1프레임 실행에서는 한 번만 저장.
- 원본/기존 출력 덮어쓰기 거부, 잘못된 인수·미지원 기능의 비정상 종료 코드.
- PowerShell 빌드 및 실제 GPU 검증 스크립트.

현재 `--hdr off`의 정확한 의미는 NVIDIA 확장을 요청하지 않는 것이다. 다음 단계의 ON/OFF 실험에는 확장 payload의 enable=0/1을 명시적으로 보내는 대조군을 추가해야 한다.

## 실행 결과

| 시험 | 결과 |
|---|---|
| Release x64 빌드 | 성공, 컴파일 경고 없음 |
| NV12 → R10G10B10A2 포맷 | 입력·출력 모두 지원 |
| BT.709 limited → RGB full BT.709 | 변환 지원, 자원 생성 성공 |
| BT.709 limited → RGB full BT.2020/PQ | 조회 HRESULT S_OK, 지원 BOOL false |
| 1920×1080 gray-ramp 120프레임 | 성공, 첫·마지막 raw SHA-256 일치 |
| 회색 계조 | 단조 증가, 채널 차이 최대 1코드값 |
| 검정·흰색 중앙값 | 검정 (0,0,0), 흰색 (1023,1023,1022) |
| 8색 color-bars | 기대 채널 순서와 full range 확인 |
| 1918×1080 행 패딩 시험 | GPU RowPitch 7680, 저장 행 7672바이트; 첫·중간·끝 행 정상 |
| 잘못된 크기·HDR ON 요청·기존 출력 경로 | 종료 코드 2로 거부 |
| 없는 NVIDIA 어댑터 | 종료 코드 3으로 거부 |

1920×1080 raw 파일 크기는 8,294,400바이트다. color-bars의 빨강 중심은 (1023,2,0), 초록은 (0,1023,3), 파랑은 (3,0,1023)으로 확인됐다. NV12의 정수 양자화와 드라이버 변환 반올림 때문에 이상적인 0/1023과 소폭 차이가 있다.

상세 산출물은 저장소의 `artifacts/stage1-validation/verification.json`과 같은 디렉터리의 raw/JSON/CSV다. 별도의 PQ 지원 조회는 `artifacts/probe-pq.json`에 있다. 바이너리·대용량 결과는 Git에서 제외한다.

## 게이트 판정과 다음 작업

- **G0: 부분 통과.** 필수 장치·뷰·SDR 변환 조합은 지원한다. 초기 PQ 조합은 일반 지원 조회에서 불지원이다.
- **G1: SDR 기준 경로 통과.** 업로드·채널·range·동기화·readback·pitch 처리가 실제 GPU에서 검증됐다.
- **G2/G3: 미실행.** HDR 효과나 색 해석은 아직 검증하지 않았다.

다음 작업은 `nvidia_hdr_extension` 모듈과 명시적 enable=0/1 비교다. 우선 지원되는 기본 조합에서 확장 호출 결과와 픽셀 변화를 관측한다. PQ 조합은 일반 API 조회가 vendor 확장 경로를 반영하는지 조사하고, 필요하다면 불지원 조회 결과를 보존하는 별도 실험 모드를 설계한다. 현재 불지원 판정을 무시하는 자동 우회는 구현하지 않았다.

이후 App 설정·Windows HDR·offscreen 의존성을 기록하며 비교한다. 색 계약이 검증된 다음에 영상 디코딩과 파일 인코딩으로 확장한다.

## 재현

PowerShell 7에서:

```powershell
./tools/build.ps1
./tools/verify-baseline.ps1
```

새 실행 디렉터리가 자동 생성된다. 스크립트는 adapter 0에서 시험하므로 GPU 구성이 바뀌면 어댑터를 확인한 뒤 수정한다. 이번 단계에서 지원하지 않는 raw 입력, HDR ON, convert 명령은 향후 명세이며 실행 가능한 기능이 아니다.
