# v0.2 성능 개선 및 비트레이트 설정

[English](../performance-v0.2.md) | [한국어 README](README.md)

2026-09-06, RTX 5080 / NVIDIA 616.56 / FFmpeg 8.0, Windows Release 빌드.

## 구현 변경

- 시작 전 `ffprobe -show_entries frame=...` 전체 디코딩 제거. 스트림 헤더만 읽고 바로 변환을 준비한다. 헤더 프레임 수/길이는 진행량 추정에만 사용하며, 정상 EOF까지 처리한다.
- 변환 디코더의 `showinfo=checksum=0`에서 프레임 번호, PTS, 시간 단위, 크기, progressive 여부, 픽셀 형식, 색 태그를 순서대로 확인한다. 메타데이터가 빠지거나 시간축이 끊기면 최종 파일을 만들지 않고 중단한다. 검사를 위해 영상을 다시 디코딩하지 않는다.
- 기본 디코딩은 D3D11VA. `hwdownload`로 P010/NV12를 받는다. 하드웨어 경로가 실패하면 중단하며, 수동 비교용 `--software-decode`를 제공한다.
- HDR RGB10 → BT.2020 limited P010 계산을 D3D11 compute shader로 이동했다. 기존과 같은 2×2 centered chroma 및 양자화 규칙을 사용하며 PQ/gamut 변환을 중복 적용하지 않는다. 일반 변환에서는 RGB를 CPU로 읽지 않는다.
- 프레임마다 만들던 P010 벡터를 재사용한다. 프로세스 파이프 요청 버퍼를 4 MiB로 늘렸다.
- 완료 시 전체 출력 디코딩은 기본 실행에서 제거했다. 인코더 정상 종료 및 출력 HEVC Main10/HDR 태그/크기를 확인한다. 완전한 출력 재디코딩 프레임 수 검사는 `--verify-full` 또는 독립 개발 검증 스크립트에서 수행한다.
- `--diagnostics`에만 raw 표본과 HDR 비교 raw를 저장한다. 기본 실행은 진단 JSON과 텍스트 로그를 남긴다.
- `result.json`에 시작 지연, 변환 평균 fps, 읽기/처리/쓰기 시간 및 실제 사용한 품질 설정을 기록한다.

## 비트레이트 선택

더블클릭/드래그 실행 후 콘솔에 `40M` 등을 입력한다. Enter는 기본 CQ 18이다.

```powershell
RTXVideoHDRConvert.exe "input.mp4" --output "video.hdr.mkv" --bitrate 40M
RTXVideoHDRConvert.exe "input.mp4" --output "quality.hdr.mkv" --cq 18
```

`40M` = 40,000,000 bit/s, `40000k`도 같은 값이다. 지원 범위는 100k..1000M이며 하드웨어/코덱이 수용하지 못하는 설정은 인코더 오류로 표시한다. `--bitrate`는 VBR 평균 목표이므로 장면에 따라 실제 비트레이트가 달라진다. CQ는 0..51이며 낮을수록 높은 품질/큰 용량을 지향한다. 기본 설정은 기존의 CQ 18과 NVENC p5/hq이며 성능을 위해 품질 설정을 낮추지 않았다. 두 모드는 동시에 지정할 수 없다. 40M은 사용 예시이며 모든 영상에 대한 권장값이 아니다.

## 측정 결과

| 입력/설정 | 프레임 | 변환 평균 | 첫 프레임까지 |
|---|---:|---:|---:|
| 제공 영상에서 추출한 4K 71.928fps 구간, GPU 색 변환, CQ 18, 진단 표본 포함 | 432 | 36.62fps | 1.43초 |
| 실제 23분 원본 파일 직접 입력, 첫 구간, GPU 색 변환, VBR 40M | 432 | 39.17fps | 1.18초 |
| 같은 추출 구간, 하드웨어 디코딩 + 기존 CPU 색 변환, CQ 18 | 48 | 3.78fps | 별도 비교 실행 |

짧은 구간의 처리 평균이며 긴 영상 전체 처리 속도 보장은 아니다. 첫 프레임 시간은 HDR 효과 검사 및 도구 초기화를 포함한다. 평균 fps는 프레임 처리 구간 기준이며 최종 오디오 복사/컨테이너 완성 시간은 제외한다. CPU 비교는 프레임 수가 다르므로 정확한 동일 조건 배율을 주장하지 않는다. 기존 약 3fps 병목이 크게 줄어든 것은 실제 처리 결과로 확인했다.

측정 기록:

- `artifacts/rtxhdr-run-102012-287938437/result.json` — 4K CQ 18 432프레임.
- `artifacts/rtxhdr-run-105352-288098140/result.json` — 원본 직접 입력 40M 432프레임.
- `artifacts/rtxhdr-run-81192-287965265/result.json` — CPU 색 변환 비교.

## 검증

- 독립 `tools/verify-conversion.py`: 4K 432프레임 전체 디코딩 수와 PTS 정상, HDR Main10/BT.2020/PQ/limited/center 정상. 오디오 284패킷 바이트 동일, 오디오 시간 오차 최대 0.333ms, 비디오 최대 0.500ms.
- 1080p 8비트 소프트웨어 디코딩 입력은 프레임 수 제한 없이 EOF까지 240프레임 처리 및 `--verify-full` 통과. 독립 검증에서 오디오 376패킷 동일, 흰색 패치 중앙값 오차 0, zscale Y 비교 최대 1코드.
- 진단 표본의 GPU P010을 동일 RGB에 대한 CPU 기준 계산과 비교하여 최대 1코드 이내 통과. 이 값은 색 변환 오차이며 손실 HEVC 압축 오차와 구분한다.
- `GpuColorTests`: 1918/1920×1080, 8/10비트 입력, 회색 램프/컬러 바 총 8조합에서 GPU/CPU P010 오차 0, 하위 6비트 0 확인.
- 타임스탬프 불연속이 있는 추출 시험 파일을 끝까지 입력했을 때 575번 프레임에서 정확히 실패. 사전 스캔 없이 검출했고 최종 출력은 생성하지 않았다.
- CTest `file_paths` 통과: 긴 경로/유니코드/덮어쓰기 방지 회귀 검증.

배포 `sample-hdr.mkv`는 이번 GPU 경로로 새로 변환한 CQ 18 약 6초 결과다.

## NVEncC에서 참고한 구조와 다음 단계

[NVEncCore.cpp](https://github.com/rigaya/NVEnc/blob/master/NVEncCore/NVEncCore.cpp)의 `initPipeline`, `allocatePiplelineFrames`, `RunEncode2`를 확인했다. 처리 단계를 연결하고 재사용 버퍼와 비동기 깊이를 관리하며 단계별 시간을 기록하는 방식을 참고했다. NVEncC 소스를 복사하거나 이 프로젝트를 NVEncC에 통합하지 않았다.

이번 수정은 하드웨어 디코딩, GPU 색 변환, 버퍼 재사용, 실시간 PTS 검증 및 단계별 측정을 적용한 것이다. 현재도 FFmpeg와 별도 프로세스이므로 디코딩 결과를 CPU로 다운로드한 뒤 D3D11에 업로드하고, HDR P010을 읽어 인코더에 전달하는 복사가 남아 있다.

다음 성능 작업은 (1) libavcodec의 D3D11 하드웨어 프레임을 직접 받아 업로드 제거, (2) D3D11 P010 텍스처를 NVENC에 직접 등록하여 출력 readback/파이프 제거, (3) 여러 프레임의 제한된 버퍼 큐와 GPU 완료 이벤트로 디코드/HDR/인코딩을 겹치는 순서다. 완전한 GPU 메모리 경로는 아직 구현하지 않았다. 현재 측정에서는 GPU 처리·readback을 포함하는 구간의 비중이 가장 크다.
