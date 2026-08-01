# Home Assistant BLE integration

이 폴더는 ESP32-S3의 사용자 정의 BLE GATT 서비스를 Home Assistant의
스위치 entity로 연결하는 custom integration입니다. MQTT와 Wi-Fi는 사용하지 않습니다.

## 전제 조건

- Home Assistant에 Bluetooth integration이 구성되어 있어야 합니다.
- Home Assistant 장치에 연결 가능한 Bluetooth adapter가 있어야 합니다.
- ESP32-S3와 Home Assistant Bluetooth adapter가 통신 범위 안에 있어야 합니다.
- 테스트할 때 LightBlue 연결은 먼저 끊어야 합니다.

Home Assistant Container를 사용하는 경우 호스트의 BlueZ/D-Bus와 Bluetooth adapter가
컨테이너에 전달되어야 합니다. Home Assistant OS에서는 Bluetooth 설정이 자동으로
처리됩니다.

## 설치

Home Assistant의 설정 디렉터리를 `<config>`라고 할 때 다음 폴더를 복사합니다.

```text
bt_practice/home_assistant/custom_components/bt_practice_ble
    -> <config>/custom_components/bt_practice_ble
```

최종 구조는 다음과 같아야 합니다.

```text
<config>/custom_components/bt_practice_ble/manifest.json
<config>/custom_components/bt_practice_ble/config_flow.py
<config>/custom_components/bt_practice_ble/switch.py
```

복사한 뒤 Home Assistant를 재시작합니다.

## 등록

1. ESP32-S3 보드를 켜서 `BT_Practice` advertising을 시작합니다.
2. LightBlue가 연결되어 있다면 Disconnect합니다.
3. Home Assistant에서 `설정 -> 장치 및 서비스`로 이동합니다.
4. 발견 목록의 `BT Practice BLE`를 선택합니다.
5. 자동 발견되지 않으면 `통합 구성요소 추가`에서 `BT Practice BLE`를 검색합니다.
6. 발견된 `BT_Practice` 장치를 선택합니다.

등록 후 `BT_Practice LED` 스위치가 생성됩니다. Home Assistant는 보드와 BLE 연결을
유지하고 LED characteristic의 notification을 구독합니다. 스위치를 켜면 GATT에
`0x01`, 끄면 `0x00`을 기존 연결로 즉시 씁니다. 상태 변화는 notification으로
동기화하므로 60초 polling을 사용하지 않습니다.

연결이 끊어지면 기존 Bleak client를 폐기하고 새 client로 재연결합니다. 재시도 간격은
1, 2, 4, 8, 15, 30초로 늘어나며, 연결되면 characteristic을 다시 읽고 notification을
다시 구독합니다. Home Assistant가 연결된 동안에는 LightBlue에서 같은 보드에 동시에
연결할 수 없습니다.

## 문제 확인

장치가 발견되지 않으면 먼저 Home Assistant의 Bluetooth integration에서 connectable
adapter가 표시되는지 확인합니다. Bluetooth proxy를 사용하는 경우 active connection을
지원해야 합니다.

Home Assistant 로그에서 custom integration 경고가 표시되는 것은 정상입니다.
직접 만든 통합은 Home Assistant 공식 내장 통합이 아니기 때문입니다.
