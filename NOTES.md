# my_app Notes Index

이 파일은 작업 메모의 색인입니다. 상세 설명은 주제별 Markdown 파일에 나눠서 정리합니다.

## 문서 관리 규칙

- 한 Markdown 파일이 1000줄을 넘으면 새 파일로 분리합니다.
- 새 내용을 추가하기 전에 기존 파일을 정리하거나 주제별 파일로 옮길 수 있습니다.
- `NOTES.md`는 길게 누적하지 않고, 전체 문서의 목차와 현재 상태 요약을 유지합니다.
- 새 설명을 추가할 때는 가장 관련 있는 주제 파일에 기록합니다.
- 앞으로도 현재 형식을 유지합니다.
  - `NOTES.md`: 색인, 현재 상태 요약, 문서 목록
  - 주제별 `*_NOTES.md`: 자세한 개념 설명, 코드 흐름, 비교 예제
  - 설명은 나중에 다시 읽어도 이해되도록 예제와 흐름 중심으로 작성
  - 코드 변경이나 새 개념을 설명하면 관련 Markdown 파일도 함께 업데이트

## 문서 목록

- [PROJECT_NOTES.md](PROJECT_NOTES.md)
  - 기본 빌드/실행 명령
  - 현재 앱 기능
  - `my_app`만 별도 Git 저장소로 관리하는 방법
  - 클래스화 전 BLE 코드 비교 파일

- [BLE_GATT_NOTES.md](BLE_GATT_NOTES.md)
  - BLE 이름 설정
  - BLE write 테스트 방법
  - `Write Request` / `Write Command`
  - BLE 코드를 클래스로 묶은 구조
  - `BT_GATT_SERVICE_DEFINE`
  - BLE UUID
  - `BT_CONN_CB_DEFINE`
  - `connection_callbacks`
  - GATT callback에서 `self`를 찾는 방식

- [CALLBACK_RUNTIME_NOTES.md](CALLBACK_RUNTIME_NOTES.md)
  - callback을 class로 묶는 대표 방식
  - `CONTAINER_OF`
  - `user_data` / context pointer
  - 전역/static 객체 직접 접근 방식
  - `bt_conn *conn` 매개변수 의미
  - BLE 연결 해제 후 advertising 재시작
  - `main()`이 return되어도 Zephyr 앱이 계속 동작하는 이유

## 현재 프로젝트 요약

현재 앱은 ESP32-S3 DevKitC에서 동작하는 Zephyr C++ 예제입니다.

주요 기능:

- WS2812 RGB LED 1개 제어
- GPIO 버튼 인터럽트 + debounce
- 짧게 누르면 색 순환
- 길게 누르면 LED off
- shell 명령으로 RGB/mode/status 제어
- BLE advertising 이름: `RGB Button`
- BLE GATT characteristic으로 RGB 값 read/write
- BLE 연결 해제 후 advertising 재시작

## 기본 명령

빌드:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu my_app -S espressif-flash-16M -S espressif-psram-8M
```

플래시:

```sh
west flash
```

모니터:

```sh
west espressif monitor
```
