2025-10-24 — SeungJae Lee

# Pages 모듈

## 개요
- 위치: `src/pages`
- 역할: 애플리케이션의 최상위 화면(UI 탭)을 구현하며, 각 매니저와 위젯 모듈을 조합해 워크플로우를 완성한다.
- 의존성: `managers`의 서비스 객체, `widgets`의 복합 위젯, Qt Widgets/Layouts, `utils` 헬퍼.

## 디렉터리 구성
- `WeldingPage.*`  
  라이브 카메라 격자, 분석 오버레이, AI/PLC 토글, 녹화 버튼을 제공하는 메인 작업 화면.
- `StoragePage.*`  
  녹화 세션을 목록·카드·재생 오버레이로 보여주고 삭제/이름 변경/폴더 열기 등 저장소 관리를 담당.
- `SettingsPage.*`  
  카메라 프로필, AI 분석 설정, PLC 연결, 언어 선택, 카메라 ROI/시리얼 튜닝을 위한 종합 설정 허브.

## 핵심 흐름
1. **WeldingPage 카메라 구성**  
   `CameraManager` 시그널을 구독해 `addCameraWidget()/removeCameraWidget()`로 미리보기 카드를 생성·재배치하고, `CameraPreviewWidget`의 이벤트를 수집해 녹화/분석 상태를 동기화한다.
2. **분석·PLC 토글 흐름**  
   `CameraPreviewWidget`에서 발생한 `aiToggleRequested`/`plcControlToggled`는 `WeldingPage`를 거쳐 `AiClient`/`PLCClient` 호출로 이어지고, 응답에 따라 미리보기/상태 패널이 갱신된다.
3. **녹화 자산 관리**  
   `StoragePage`가 `RecordingManager::recordingAdded/Removed`를 감지해 세션 모델을 재구성하고, 선택된 세션을 `SessionPlaybackWidget`으로 재생하거나 요약 수치를 집계한다.
4. **설정 편집**  
   `SettingsPage`는 카메라/PLC 목록을 동적으로 빌드하고, 사용자의 입력을 검증한 뒤 `ConfigUtils`와 `AiClient::setSettings()` 등을 통해 저장한다. 시리얼 카메라 옵션은 백그라운드 스레드(QFuture)와 BusyOverlay로 처리한다.
5. **언어 전환**  
   언어 선택 변경 시 `LocalizationManager::applyLanguage()`를 호출하고 각 페이지의 `retranslateUi()`를 통해 텍스트를 즉시 재적용한다.
6. **ROI 범례 제어**  
   `config.json`의 `global.showLegend` 값에 따라 Welding/Settings/재생 페이지에 동일한 ROI 범례가 표시되며, 값이 없으면 기본값 `true`로 저장된다.

## 확장 포인트
- 신규 페이지 추가 시 `MainWindow::setupPages()`에 `QStackedWidget` 등록과 탐색 버튼 세트를 추가하면 된다.
- `WeldingPage`는 `connectPreviewSignals()`를 통해 프리뷰 위젯과 느슨하게 결합되어 있으므로 다른 미리보기 타입을 도입할 때 이 헬퍼를 재사용한다.
- `StoragePage`의 세션 모델은 `RecordingSession` 구조체 기반이므로 메타데이터 확장 시 구조체와 관련 집계 함수(`updateStorageSummary`)만 수정하면 된다.
- `SettingsPage`는 섹션별 빌더(`buildCameraSection`, `buildAnalysisSection` 등)를 분리해 두었으므로 새로운 설정 그룹은 동일한 패턴으로 추가한다.

## 주요 시그널 / 콜백
- `WeldingPage::frameCaptured(cameraId, frame)`  
  녹화 매니저나 AI 업로드 파이프라인이 최신 프레임을 받는 진입점.
- `WeldingPage::aiToggleRequested(cameraId, enabled)`  
  상위(MainWindow)에서 AI 세션 제어 로직을 결합하는 트리거.
- `SettingsPage::setCameraAnalysisEnabled(const QString &cameraId, bool enabled, const QString &url, const QString &streamKey, double fps)` / `cameraAnalysisConfig()`  
  다른 모듈이 분석 엔드포인트 정보를 갱신·조회하고, `MainWindow`에서 단일 활성 AI 카메라 정책을 유지할 때 사용.
- `StoragePage`는 외부로 시그널을 내보내지 않지만, `RecordingManager`와 `AiClient` 이벤트를 감싼 내부 슬롯을 통해 UI를 최신 상태로 유지한다.
