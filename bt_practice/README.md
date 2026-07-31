# Bluetooth Practice

ESP32-S3 DevKitC에서 Zephyr Bluetooth LE를 단계별로 실습하는 프로젝트입니다.

## 진행 순서

1. BLE 초기화와 advertising 확인 (완료)
2. 휴대폰에서 연결/해제 및 로그 확인 (완료)
3. GATT characteristic read/write 실습 (완료)
4. BLE 명령으로 보드 RGB LED 제어 (완료)
5. 상태 notification 실습 (현재 단계)
6. Home Assistant 연동 방식 결정 및 구현

현재 코드는 `BT_Practice`라는 이름으로 connectable advertising을 시작하고,
연결/해제 정보를 콘솔에 기록합니다. 사용자 정의 GATT characteristic에
1바이트 값을 쓰거나 읽어 보드의 RGB LED를 제어할 수 있습니다.

## GATT UUID

```text
LED Control Service
9f1d1000-3d2f-4f3a-8b11-123456789abc

LED Control Characteristic (Read, Write, Notify)
9f1d1001-3d2f-4f3a-8b11-123456789abc
```

Characteristic 값은 1바이트입니다.

```text
0x00: RGB LED 끄기
0x01: RGB LED 파란색 켜기
```

## 대상 보드

- Board: `esp32s3_devkitc/esp32s3/procpu`
- Flash snippet: `espressif-flash-16M`
- PSRAM snippet: `espressif-psram-8M`

## 빌드 명령

빌드는 사용자 확인 후 실행합니다.

```sh
west build -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  bt_practice \
  -S espressif-flash-16M \
  -S espressif-psram-8M
```

이후 실행 명령은 다음과 같습니다.

```sh
west flash
west espressif monitor
```

## 첫 동작 확인

1. 보드의 serial monitor에서 Bluetooth 초기화 및 advertising 로그를 확인합니다.
2. 휴대폰의 BLE 탐색 앱에서 `BT Practice`를 찾습니다.
3. 연결한 뒤 monitor의 `Connected` 로그를 확인합니다.
4. 연결을 끊고 `Disconnected` 로그를 확인합니다.

일반 Bluetooth 설정 화면보다 nRF Connect 같은 BLE GATT 탐색 앱이 확인에 편리합니다.

## LightBlue에서 LED 제어

1. `BT_Practice`를 선택하고 연결합니다.
2. `9f1d1000-...` 서비스를 찾습니다.
3. 그 아래의 `9f1d1001-...` characteristic을 선택합니다.
4. Write 형식을 Hex로 설정하고 `01`을 전송하면 LED가 켜집니다.
5. `00`을 전송하면 LED가 꺼집니다.
6. Read를 실행하면 현재 상태가 `01` 또는 `00`으로 표시됩니다.

## LightBlue에서 notification 확인

1. LED Control characteristic의 `Listen for notifications`를 활성화합니다.
2. 콘솔에서 `LED notifications enabled` 로그를 확인합니다.
3. Hex 값 `01` 또는 `00`을 Write합니다.
4. LightBlue의 notification 기록에 같은 값이 수신되는지 확인합니다.
5. 보드 콘솔에는 `Notification sent: 1` 또는 `Notification sent: 0`이 표시됩니다.

구독하지 않은 상태에서도 Read와 Write는 정상 동작합니다. 이 경우 보드에는
`Notification skipped: client is not subscribed` 로그가 표시됩니다.
