2025-10-24 — SeungJae Lee

# Managers 모듈

## 개요
- 위치: `src/managers`
- 역할: 카메라·AI·녹화·PLC·다국어 등 애플리케이션의 상태ful 서비스를 캡슐화해 UI 페이지와 외부 장비/서비스 사이를 중재한다.
- 의존성: Qt Multimedia(카메라/녹화), Qt Network(HTTP/UDP), Qt Core(신호/슬롯, JSON), 내부 유틸리티(`utils`, `i18n`).

## 디렉터리 구성
- `AiClient.*`  
  AI 분석 서버와의 HTTP 통신, 설정 로딩, 프레임 업로드를 담당.
- `AiClientPayloadBuilder.*`  
  모델 기동 시 필요한 JSON 페이로드를 구성하는 헬퍼.
- `CameraManager.*`  
  카메라 메타데이터·표시 상태·Qt 카메라 핸들을 중앙에서 보관하고 변경 시그널을 내보냄.
- `LocalizationManager.*`  
  다국어 리소스를 검색하고 적용하며 `JsonTranslator`를 통해 Qt 번역기를 설치.
- `PLCClient.*`  
  용접 PLC와 UDP 통신을 수행해 연결 상태, 이동 명령, 편차 전송 등을 처리.
- `RecordingManager.*`  
  카메라 녹화/스냅샷 세션을 관리하고 메타데이터 및 분석 결과 파일을 유지.

## 핵심 흐름
1. **AI 분석 파이프라인**  
   `AiClient::modelStartup()` → 서버와 모델 협상 → `sendFrame()`/`analyzeData()`로 프레임 업로드 및 결과 수신 → `analysisFinished` 시 UI/저장소에 전달.
2. **카메라 라이프사이클**  
   `CameraManager::addCamera()`로 등록된 장치를 추적하고, `assignCameraHandle()`로 실제 `QCamera` 인스턴스를 공유해 `CameraPreviewWidget`과 연결.
3. **녹화 세션**  
   `RecordingManager::startRecording()`이 `QMediaRecorder`/`QMediaCaptureSession`을 구성 → 종료 시 `recordingAdded` 시그널과 메타파일 기록 → 스냅샷/분석 결과와 결합.
4. **PLC 연동**  
   `PLCClient::connectToController()`로 컨트롤러 접속을 관리하고, `sendDeviation()`으로 편차 보정 명령을 전송하며 상태 변화를 `connectionStateChanged`로 방송.
5. **언어 변경**  
   `LocalizationManager::applyLanguage()`가 JSON 번역 파일을 로드하고 Qt 번역기에 등록 → `languageChanged` 신호로 UI가 재번역됨.

### 다중 녹화 동기화 전략
- `RecordingManager::startRecording()`은 각 카메라별 `ActiveRecording`에 UTC 기준 시작 시각과 `QElapsedTimer`를 저장합니다.  
  이후 `appendAnalysisResult()`가 호출되면 이 타이머를 기반으로 `timestampMs` 필드를 자동 채워 분석 결과가 녹화 시작 시점을 기준으로 정렬됩니다.
- 녹화 종료 시 `finalizeRecording()`이 `RecordingMetadata::durationMs`, `startedAt`, `finishedAt`을 메타파일로 기록하고, `loadExistingRecordings()`가 다시 읽어 세션 목록을 구성합니다.
- 두 개 이상의 세션을 동기 재생하려면 각 세션의 `RecordingMetadata.startedAt`과 개별 분석 항목의 `timestampMs`를 사용해 공통 타임라인을 계산하면 됩니다.  
  `StoragePage`/`SessionPlaybackWidget`은 `.analysis.json`에 저장된 이 값을 사용해 타임라인 오버레이를 구성합니다.
- 추가 정밀도가 필요하면 녹화를 시작할 때 PLC 트리거나 외부 이벤트를 동시에 기록해 후처리 시 공통 기준점을 맞출 수 있습니다.
- `.analysis.json` 파일은 각 줄마다 녹화 시작 이후 경과 시간을 `timestampMs`로 포함하고, 이 값은 재생 시점에 `SessionPlaybackWidget`에서 곧바로 위치 비교에 사용됩니다.

## 확장 포인트
- 새 장비/서비스 매니저 구현 시 `QObject` 기반으로 신호를 정의하고 UI와의 의존성을 느슨하게 유지할 것.
- `AiClient`는 `currentConfig()`/`setSettings()`로 외부 설정을 주입받으므로 모델 옵션 확장 시 이 구조를 재사용한다.
- `RecordingManager`는 세션 디렉터리를 자동 정리하므로 출력 구조 변경 시 관련 헬퍼(`buildVideoFilePath`, `metadataFilePathFor`)만 수정하면 된다.
- `PLCClient`는 주소/프레임 빌더를 별도 메서드로 분리해 두었으므로 다른 PLC 프로토콜 지원 시 해당 부분을 교체하는 어댑터를 추가할 수 있다.

## 주요 시그널
- `AiClient::analysisFinished(ok, results, message)`  
  분석 완료 이벤트. 실패 시에도 호출되어 UI가 상태를 갱신 가능.
- `CameraManager::cameraUpdated(info)` / `cameraVisibilityChanged(id, visible)`  
  카메라 카드와 페이지 레이아웃이 재빌드될 때 구독.
- `RecordingManager::recordingStarted(cameraId)` / `recordingAdded(metadata)`  
  UI와 저장소 페이지가 실시간으로 녹화 목록을 동기화하는 데 사용.
- `PLCClient::connectionStateChanged(id, state, message)`  
  PLC 연결 UI와 안전 장치가 상태 변화를 감지.
- `LocalizationManager::languageChanged(code)`  
  `MainWindow`와 각 페이지의 `retranslateUi()` 호출 트리거.
