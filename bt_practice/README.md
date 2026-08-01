# Bluetooth Fan Controller Practice

ESP32-S3 DevKitC와 Zephyr Bluetooth LE로 3단 기계식 선풍기를 제어하는
실습 프로젝트입니다. Home Assistant가 BLE 연결을 유지하며, ESP32-S3는
Active High 릴레이 네 개를 제어합니다.

## 릴레이 연결

현재 기본 GPIO 배치는 다음과 같습니다.

| 기능 | ESP32-S3 GPIO | 동작 |
|---|---:|---|
| 풍속 1 | GPIO4 | High인 동안 릴레이 ON |
| 풍속 2 | GPIO5 | High인 동안 릴레이 ON |
| 풍속 3 | GPIO6 | High인 동안 릴레이 ON |
| 회전 | GPIO7 | High인 동안 릴레이 ON |

핀 배치는 `boards/esp32s3_devkitc_procpu.overlay`에서 변경할 수 있습니다.
부팅할 때 네 출력은 모두 inactive, 즉 Low로 초기화됩니다. GPIO가 부팅 중
떠서 릴레이가 순간적으로 켜지는 것을 막으려면 각 입력에 외부 풀다운 저항을
사용하는 것이 좋습니다.

풍속 릴레이는 항상 하나만 켜집니다. 풍속을 바꿀 때 기존 릴레이를 포함한
세 릴레이를 모두 끄고 100ms 후 선택한 릴레이를 켭니다. 회전 릴레이는
풍속과 독립적이며 회전 ON 상태인 동안 계속 켜져 있습니다. 선풍기 OFF
명령은 풍속과 회전 릴레이를 모두 끕니다.

> 릴레이 접점이 선풍기의 상용 전압 회로에 연결된다면 ESP32와 선풍기 회로를
> 공통 GND로 연결하지 마십시오. 모터 부하에 맞는 접점 정격, 절연거리, 퓨즈,
> 난연 케이스가 필요합니다. 전원이 분리된 상태에서 배선하십시오.

## BLE GATT 구성

```text
Fan Service
9f1d1000-3d2f-4f3a-8b11-123456789abc

Fan Command (Write)
9f1d1001-3d2f-4f3a-8b11-123456789abc

Fan State (Read, Notify)
9f1d1002-3d2f-4f3a-8b11-123456789abc
```

명령은 2바이트 `[명령, 값]`입니다.

```text
01 00  선풍기 풍속 OFF
01 01  풍속 1
01 02  풍속 2
01 03  풍속 3
02 00  회전 OFF
02 01  회전 ON
03 00  선풍기 전체 OFF(풍속과 회전 모두 OFF)
```

상태도 2바이트이며 `[현재 풍속, 회전 상태]`입니다. 예를 들어 `02 01`은
풍속 2와 회전 ON을 뜻합니다. 릴레이 상태가 적용된 뒤 notification을 보내므로
Home Assistant의 상태가 보드 상태와 동기화됩니다.

## 대상 보드와 빌드

- Board: `esp32s3_devkitc/esp32s3/procpu`
- Flash snippet: `espressif-flash-16M`
- PSRAM snippet: `espressif-psram-8M`

빌드와 플래시는 사용자가 확인 후 실행합니다.

```sh
west build -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  bt_practice \
  -S espressif-flash-16M \
  -S espressif-psram-8M

west flash
west espressif monitor
```

## Home Assistant

MQTT나 Wi-Fi 없이 Home Assistant가 GATT service에 직접 연결하는 custom
integration은 `home_assistant/`에 있습니다. 설치 방법은
[`home_assistant/README.md`](home_assistant/README.md)를 참고합니다.
