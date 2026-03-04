2025-10-24 — SeungJae Lee

# Widgets 모듈

## 개요
- 위치: `src/widgets`
- 역할: 메인 UI에서 재사용되는 커스텀 위젯 집합으로, 카메라 미리보기·분석 오버레이·토글 스위치 등 시각 요소를 제공함.
- 의존성: Qt Widgets(레이아웃, 이벤트 시스템), Qt Multimedia(카메라/비디오), 프로젝트 내 유틸리티(`WeldAlignmentWidget`, `ZoomableVideoView` 등).

## 디렉터리 구성
- `CameraPreviewWidget.*`  
  카메라 스트림, 분석 오버레이, 상태 패널을 하나의 카드 UI로 묶는 복합 위젯.
- `AnalysisStatusPanel.*`  
  분석 상태(패스 정보, 정렬 오프셋, 사용자 토글 섹션)를 표시하는 사이드 패널.
- `AnalysisOverlayWidget.*`  
  영상 위에 ROI·검출 결과를 그리는 오버레이 레이어. ROI 범례는 `ConfigUtils::showLegend()` 값에 따라 표시된다.
- `RoiStyleUtils.*`  
  ROI 테두리/섹션/범례를 공통 스타일로 그리기 위한 헬퍼.
- `ZoomableVideoView.*`  
  비디오 스트림의 패닝/줌 동작을 담당하는 뷰포트.
- `ToggleSwitch.*`  
  온·오프 토글을 시각적으로 표현하는 커스텀 스위치.
- 기타 단일 기능 위젯(예: `PlaybackDeviationBar`, `BusyIndicator`)은 독립적인 UI 요소를 제공하며 상위 화면에서 조합함.

## 핵심 흐름 (CameraPreviewWidget 기준)
1. **UI 초기화**  
   `buildUi()`에서 비디오 컨테이너, 오버레이 레이어, 헤더/풋터 패널을 배치한다. 생성과 동시에 Zoom 버튼, PLC/AI 토글 등을 설정한다.
2. **미디어 세션 연결**  
   `QMediaCaptureSession`을 생성하여 `ZoomableVideoView`의 `QVideoWidget`(`videoItem()`)과 연결하고, `QVideoSink`를 통해 프레임 변화를 수신한다.
3. **프레임 처리**  
   `handleVideoFrameChanged()`에서 최신 프레임을 받아 뷰 사이즈를 조정하고 `frameAvailable` 시그널로 상위 모듈에 전달한다.
4. **분석 오버레이 업데이트**  
   `setAnalysisOverlay()`로 전달된 ROI, 도형 데이터를 `AnalysisOverlayWidget`에 반영하고 AI 토글 상태에 따라 가시성을 제어한다.
5. **상태 패널 동기화**  
   `setAlignmentOffset()`, `setPlcControlEnabled()` 등 setter 호출 시 `AnalysisStatusPanel`과 헤더 배지를 갱신하고, 필요한 시그널을 상위로 송신한다.
6. **이벤트 포워딩**  
   오버레이 레이어에서 직접 처리하지 않는 마우스/휠 이벤트는 `forwardMouseEventToVideoView()`/`forwardWheelEventToVideoView()`로 비디오 뷰포트에 전달해 사용자 인터랙션을 유지한다.
7. **범례 표시 제어**  
   `setShowLegend()`를 통해 ROI 범례를 숨기거나 표시할 수 있으며, Welding/Settings/재생 화면 모두 `global.showLegend` 설정을 공유한다.
8. **재생 오버레이 타이밍**  
   `SessionPlaybackWidget`은 `.analysis.json`에 기록된 `timestampMs`(녹화 시작 이후 경과 시간)를 활용해 도트/ROI를 재생 위치에 맞춰 표시하고, 다중 카메라 세션은 `alignmentOffsetMs`로 녹화 시작 시각을 보정한다.

## 확장 포인트
- 새로운 분석 인디케이터를 추가할 때는 `AnalysisStatusPanel::addCustomSection()`을 사용하여 UI를 동적으로 삽입한다.
- 카메라별 설정 뱃지는 `CameraManager::CameraInfo::settings`를 바탕으로 `rebuildSettingIndicators()`에서 구성하므로, 설정 항목 확장 시 해당 구조체를 확장하면 된다.
- 스타일 변경은 `CameraPreviewWidget::updateOverlayChrome()`과 각 위젯의 `setStyleSheet()` 호출부에서 일관되게 관리할 수 있다.

## 참고 시그널
- `frameAvailable(cameraId, QImage)`: 최신 프레임을 외부 처리기(녹화, 분석 등)에 전달.
- `plcControlToggled(cameraId, enabled)` / `aiAnalysisToggled(cameraId, enabled)`: 사용자 토글 변경 시 상위 로직과 동기화.
- `alignmentOffsetAdjusted(cameraId, offsetMm)`: 키보드/버튼 조작으로 정렬 보정이 이루어졌음을 알림.
