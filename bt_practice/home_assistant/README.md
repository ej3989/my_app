# Home Assistant BLE fan integration

ESP32-S3의 BLE GATT 서비스를 Home Assistant의 `fan` entity로 연결하는
custom integration입니다. MQTT와 Wi-Fi는 사용하지 않습니다.

## 설치 전제 조건

- Home Assistant에 Bluetooth integration이 구성되어 있어야 합니다.
- Home Assistant 장치에 connectable Bluetooth adapter가 있어야 합니다.
- ESP32-S3와 Bluetooth adapter가 통신 범위 안에 있어야 합니다.
- Home Assistant가 연결을 유지하므로 LightBlue 연결은 먼저 끊어야 합니다.

Home Assistant Container에서는 호스트의 BlueZ/D-Bus와 Bluetooth adapter가
컨테이너에 전달되어야 합니다.

## 설치 또는 업데이트

Raspberry Pi의 Home Assistant 설정 디렉터리가
`/home/ej3989/docker/homeassistant/config`인 현재 환경에서는 이 폴더를 복사합니다.

```text
bt_practice/home_assistant/custom_components/bt_practice_ble
  -> /home/ej3989/docker/homeassistant/config/custom_components/bt_practice_ble
```

최종 파일에는 `fan.py`가 있어야 합니다. 이전 버전의 `switch.py`가 Raspberry Pi에
남아 있으면 삭제한 뒤 Home Assistant 컨테이너를 재시작합니다.

## 등록과 동작

기존에 `BT Practice BLE` 통합을 등록했다면 파일을 업데이트하고 재시작하는 것으로
충분합니다. 처음 설치했다면 다음 순서로 등록합니다.

1. ESP32-S3에서 새 펌웨어를 실행합니다.
2. Home Assistant의 `설정 → 장치 및 서비스`로 이동합니다.
3. 발견된 `BT Practice BLE`를 추가합니다.
4. 자동 발견되지 않으면 통합 구성요소 추가에서 직접 선택합니다.

등록 후 하나의 선풍기 entity가 생성됩니다.

- 켜기: 마지막에 사용한 풍속으로 켭니다. 최초 기본값은 풍속 1입니다.
- 끄기: 풍속 릴레이와 회전 릴레이를 모두 끕니다.
- 풍속: Home Assistant의 33%, 67%, 100%가 각각 풍속 1, 2, 3입니다.
- 회전: 회전 ON/OFF가 Active High 릴레이 상태에 직접 대응합니다.

Home Assistant는 ESP32-S3와 BLE 연결을 유지하고 Fan State notification을
구독합니다. 연결이 끊어지면 1, 2, 4, 8, 15, 30초 간격으로 재연결하며,
연결되면 현재 상태를 읽고 notification을 다시 구독합니다.

이전 버전에서 생성한 LED switch entity는 새 fan entity로 자동 변환되지 않습니다.
Home Assistant entity registry에 비활성 상태로 남는다면 해당 이전 switch entity만
삭제해도 됩니다.

## 문제 확인

장치가 발견되지 않으면 Home Assistant의 Bluetooth integration에 connectable
adapter가 표시되는지 확인합니다. Bluetooth proxy는 active connection을 지원해야
합니다. 직접 만든 custom integration이라는 경고는 정상입니다.
