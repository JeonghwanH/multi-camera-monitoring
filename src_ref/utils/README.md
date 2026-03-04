2025-10-24 — SeungJae Lee

# Utils 모듈

## 개요
- 위치: `src/utils`
- 역할: 설정 로딩, 로깅, 문자열 정규화 등 공통 기능을 제공해 UI/매니저 코드의 중복을 줄인다.
- 의존성: Qt Core(JSON, IO, 문자열), 표준 라이브러리.

## 디렉터리 구성
- `ConfigUtils.*`  
  앱 설정 JSON을 읽고 쓰며, 사용자 설정 파일 경로를 계산한다.
- `Logger.*`  
  전역 Qt 메시지 핸들러를 설치해 로그 파일에 기록하고, 이전 핸들러와 스레드 안전성을 유지한다.
- `StringUtils.*`  
  스트림 키/별칭용 문자열 클린업 헬퍼(대소문자·공백 정리 등)를 제공한다.

## 사용 패턴
1. **구성 로딩**  
   `ConfigUtils::loadConfig()`를 애플리케이션 초기화(MainWindow, AiClient 등)에서 호출해 사용자 설정을 반영한다.
2. **설정 저장**  
   설정 페이지에서 편집을 마친 뒤 `ConfigUtils::saveConfig()`로 JSON 문서를 덮어쓰고, 필요한 폴더가 없으면 자동 생성한다.
3. **로그 초기화**  
   `Logger::initialize(appName)`을 시작 시점에 한 번 호출하면 모든 `qInfo()/qWarning()` 로그가 파일과 콘솔에 동시에 기록된다.
4. **문자열 정규화**  
   카메라/세션 별칭 생성 시 `StringUtils::streamKeyFromAlias()`로 분석 서버에 안전한 키를 생성한다.
5. **ROI 범례 제어**  
   `ConfigUtils::showLegend()`로 `global.showLegend` 값을 조회하면 모든 페이지에서 ROI 범례 표시 여부를 통일할 수 있다. 값이 없으면 함수가 기본값 `true`를 기록한다.

## 확장 포인트
- 새 헬퍼 추가 시 독립 네임스페이스를 사용하고, Qt 의존성이 있다면 `.cpp`에서만 include하여 빌드 타임 영향을 줄인다.
- `Logger`는 현재 단일 파일 출력만 지원하므로 회전 로깅 등 고급 기능이 필요하면 내부 스트림 관리부(`messageHandler`)를 확장하면 된다.
- 설정 구조가 복잡해질 경우 `ConfigUtils`에 스키마 검증 함수를 추가해 SettingsPage 저장 단계에서 오류를 조기에 검출할 수 있다.
