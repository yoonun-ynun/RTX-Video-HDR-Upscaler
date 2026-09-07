# v0.4.0 GPU 직접 전달 검증

[English](../performance-native-v0.4.md) | [한국어 README](README.md)

## 구현

기본 경로는 동일 D3D11 장치의 하드웨어 디코딩 텍스처를 HDR 처리에 전달하고, compute shader가 P010 텍스처의 Y/UV plane에 직접 기록한다. FFmpeg hevc_nvenc는 해당 GPU surface를 사용한다. 원본 프레임과 인코더 입력을 CPU로 내려받는 두 왕복을 제거했다. GPU 내부의 텍스처 복사는 남아 있다. NVENC는 surface 8개와 지연 4프레임으로 처리한다. 기본 CQ/비트레이트와 p5/hq 설정을 유지했다.

`--pipe-video`는 이전 입력·GPU·출력 겹치기 경로, `--serial-pipeline`은 순차 경로다. `--diagnostics`, `--cpu-color`, `--software-decode`는 비교용 파이프 경로를 사용한다. 직접 경로의 프로세스 내부 오류는 native.log, native-config.json 및 error.json 메시지에 기록한다.

## 확인한 결과

- RTX 4060, 4K 10비트 SDR, 71.928fps 입력 432프레임, CQ18: GPU 직접 전달 약 96.7fps. 기존 순차 경로 3회 중앙값 22.88fps, 파이프 겹치기 35.47fps. 당시 시스템 부하와 짧은 구간에 대한 측정이며 모든 GPU/영상의 향상률을 보장하지 않는다.
- 순차 결과와 직접 결과의 432프레임 전체 디코딩: 픽셀 해시·타임스탬프 동일. HDR Main10/BT.2020/PQ/limited/center 및 오디오 패킷 해시·시간 동일.
- 5분 연속 1080p 10비트 시험 영상 9,000프레임: 변환·mux 완료, 전체 출력 재디코딩 검증 통과. 처리 구간 약 25.75초(349.5fps), 최종 검증 시간 별도.
- 1프레임·2프레임의 flush와 전체 프레임 수 검증 통과. 575번 프레임에 시간축 오류가 있는 파일은 거부하고 최종 파일을 생성하지 않음.
- GPU 색 변환 8가지 조합(1918/1920 너비, 8/10비트, 램프/컬러바): 기존 P010과 직접 텍스처 P010 바이트 동일, CPU 기준 오차 0.
- GUI MKV/VBR, MP4/CQ, 단계 표시 및 취소 통과. 설치 전 GUI 안내, Windows PowerShell 5.1 설치 도구, 개인 설정 보존, 설치된 패키지의 실제 MP4 변환 및 전체 검증 통과.
- CTest 3개(경로, FPS, 자식 프로세스 오류 진단) 통과.

시험 중 발견한 Matroska 프레임률 기록 누락을 수정해 명시적인 avg_frame_rate/r_frame_rate를 기록한다. 최종 비교는 이 수정 이후 결과로 통과했다.

## 구성 및 재현

FFmpeg 공유 라이브러리는 날짜 고정 BtbN 8.1.2 빌드를 사용한다. 출처·SHA256은 tools/ffmpeg-native-lock.json에 있다. 릴리즈는 FFmpeg 바이너리를 포함하지 않으며 설치 도구가 공식 배포처에서 검증 후 받는다. 테스트한 DLL은 날짜 고정 패키지의 DLL과 모두 같은 해시였다.

빌드: `tools/build.ps1 -FFmpegRoot <압축 해제한 개발 패키지> -BuildDirectory build-overlap`.
픽셀/시간/오디오 비교: `python tools/verify-overlap.py <순차.mkv> <직접.mkv> --frames 432`.
하드웨어 색 검증: `build-overlap/Release/GpuColorTests.exe`.

로컬 증거: artifacts/native-fixed-rate-verify.txt, artifacts/native-long-tagged.txt, artifacts/rtxhdr-run-25180-11660109/result.json, artifacts/packaged-native.txt. 원본 영상·개인 경로가 담긴 로그·시험 결과물은 GitHub에 업로드하지 않는다.
## 전역 FFmpeg 검색 확인

v0.4.0 GUI는 프로그램 폴더와 PATH를 검색한다. 전역 ffmpeg/ffprobe 및 공유 DLL이 모두 있을 때 설치 안내 없이 GPU 열거가 성공하는 것을 확인했다. 실행 파일만 있을 때는 FFmpeg를 찾았음을 표시하고 GPU 처리 DLL 추가만 안내한다. Windows PowerShell 설치 도구가 기존 전역 ffmpeg/ffprobe를 재설치하지 않는 것도 검증했다.
