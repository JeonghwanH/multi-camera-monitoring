2025-10-24 — SeungJae Lee

# i18n 모듈

## 개요
- 위치: `src/i18n`
- 역할: JSON 기반 번역 리소스를 로드해 Qt 번역 시스템과 통합한다.
- 의존성: Qt Core/Gui(번역기), `utils/ConfigUtils` 및 `managers/LocalizationManager`에서 소비.

## 디렉터리 구성
- `JsonTranslator.*`  
  JSON 파일에서 문자열 맵을 읽어 `QTranslator` 인터페이스를 구현한 커스텀 번역기.

## 동작 방식
1. `LocalizationManager::applyLanguage()`가 선택된 언어의 JSON 파일 경로를 결정한다.
2. `JsonTranslator::loadFromFile()`이 컨텍스트별 번역 맵을 메모리에 적재하고 언어 메타데이터를 저장한다.
3. 번역기가 Qt 애플리케이션에 설치되면 `translate()` 오버라이드를 통해 위젯 텍스트가 즉시 대체된다.

## 확장 포인트
- JSON 스키마에 성/수 변형, 지역별 포맷 등을 추가하려면 `lookup()`과 `loadFromFile()` 구문을 확장한다.
- 캐싱 전략이나 동시 언어 로딩이 필요하면 `LocalizationManager`에서 복수 번역기를 관리하도록 조정한다.
