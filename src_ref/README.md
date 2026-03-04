2025-10-24 — SeungJae Lee

# src 디렉터리 개요

## 개요
- 역할: Qt 기반 Weldbeing V 애플리케이션의 전체 C++ 소스가 위치하며, 엔트리 포인트와 UI/서비스 모듈로 구성된다.
- 주요 의존성: Qt Widgets/Multimedia/Network, 내부 `managers`, `widgets`, `pages`, `utils`, `i18n`.

## 구조
- `main.cpp`  
  애플리케이션 엔트리. `QApplication` 초기화, `Logger::initialize`, `MainWindow` 생성 및 실행.
- `MainWindow.*`  
  내비게이션과 페이지 스택, 매니저 객체의 생명주기, 권한 요청, 글로벌 시그널 조율을 담당.
- `widgets/`  
  재사용 가능한 UI 구성요소(카메라 프리뷰, 분석 패널, 토글 등). 상세 설명은 하위 README 참조.
- `pages/`  
  최상위 화면(용접, 저장소, 설정)을 포함. 각 README에 페이지별 흐름 정리.
- `managers/`  
  카메라/AI/PLC/녹화/로컬라이제이션과 같은 상태ful 서비스 계층.
- `utils/`  
  설정 파일 입출력, 로깅, 문자열 헬퍼 등 공통 기능.
- `i18n/`  
  JSON 번역 리소스를 다루는 커스텀 번역기.

## 초기화 흐름
1. `main.cpp`에서 `QApplication`과 로거, 스타일 관련 전역 설정을 완료한다.
2. `MainWindow` 생성 시 필요한 매니저(`CameraManager`, `RecordingManager`, `AiClient`, `PLCClient`, `LocalizationManager`)를 구성하고, 페이지/위젯과 연결한다.
3. 앱 시작과 동시에 언어 설정, AI 상태, 카메라 권한 체크, 카메라 디바이스 스캔이 진행된다.
4. 이후 페이지별 상호작용은 각 모듈 README에 기술된 흐름을 따른다.

## 확장 가이드
- 새로운 기능을 추가할 때는 우선 적절한 디렉터리(예: 재사용 UI면 `widgets`, 서비스 계층이면 `managers`)에 배치하고 README를 갱신해 문서 일관성을 유지한다.
- `MainWindow`는 매니저 생명주기의 단일 책임자이므로, 신규 매니저 추가 시 생성·연결·정리 루틴을 이 클래스에 등록한다.
