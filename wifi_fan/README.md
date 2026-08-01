# Wi-Fi MQTT Fan Controller

ESP32-S3 DevKitC, Zephyr Wi-Fi, MQTT를 이용해 3단 기계식 선풍기를 제어하는
독립 프로젝트입니다. 기존 `bt_practice` 프로젝트는 변경하지 않고 보존합니다.

## 전체 구조

```text
Home Assistant ── MQTT ── Mosquitto (192.168.0.66)
                              │
                         Wi-Fi 공유기
                              │
                          ESP32-S3
                              │
                 Active High 릴레이 4개
                              │
                           선풍기
```

Home Assistant MQTT discovery를 사용하므로 별도의 custom component가 필요하지
않습니다. MQTT에 연결되면 다음 entity가 자동으로 생성됩니다.

- `Bedroom Fan`: 전원, 풍속 1·2·3, 회전 ON/OFF
- `Wi-Fi RSSI`: 30초마다 갱신되는 Wi-Fi 신호 세기(dBm) 진단 센서

## GPIO 배치

| 기능 | ESP32-S3 GPIO | 동작 |
|---|---:|---|
| 풍속 1 | GPIO4 | High인 동안 릴레이 ON |
| 풍속 2 | GPIO5 | High인 동안 릴레이 ON |
| 풍속 3 | GPIO6 | High인 동안 릴레이 ON |
| 회전 | GPIO7 | High인 동안 릴레이 ON |

풍속 변경은 세 풍속 릴레이를 모두 끄고 100ms 후 하나만 켜는
break-before-make 방식입니다. 전체 OFF 명령은 풍속과 회전 릴레이를 모두
끕니다. 부팅 시에도 모든 릴레이가 OFF로 초기화됩니다.

## 1. 개인 설정 파일 만들기

예제 파일을 복사합니다.

```sh
cd /Volumes/ej_disk/zephyrproject/EJ_APP/wifi_fan
cp wifi_fan_private.conf.example wifi_fan_private.conf
```

`wifi_fan_private.conf`를 열어 실제 정보를 입력합니다.

```conf
CONFIG_WIFI_FAN_WIFI_SSID="YOUR_WIFI_SSID"
CONFIG_WIFI_FAN_WIFI_PASSWORD="YOUR_WIFI_PASSWORD"

CONFIG_WIFI_FAN_MQTT_HOST="192.168.0.66"
CONFIG_WIFI_FAN_MQTT_PORT=1883
CONFIG_WIFI_FAN_MQTT_USERNAME=""
CONFIG_WIFI_FAN_MQTT_PASSWORD=""
```

Mosquitto가 인증을 요구하면 사용자 이름과 비밀번호를 입력합니다. 이 파일은
`.gitignore`에 등록되어 Git에 포함되지 않습니다.

현재 구현은 로컬 네트워크의 일반 MQTT 포트 1883을 사용합니다. 인터넷에 MQTT
포트를 노출하지 마십시오. 외부 네트워크를 통과해야 한다면 TLS 구성이 필요합니다.

## 2. 빌드와 실행

빌드와 플래시는 사용자가 확인 후 실행합니다.

```sh
cd /Volumes/ej_disk/zephyrproject/EJ_APP

west build -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  wifi_fan \
  -S espressif-flash-16M \
  -S espressif-psram-8M \
  -- -DEXTRA_CONF_FILE=wifi_fan_private.conf

west flash
west espressif monitor
```

정상 로그 흐름은 다음과 같습니다.

```text
Wi-Fi fan controller start
All active-high relays initialized OFF
Connecting to Wi-Fi SSID '...'
Connected to Wi-Fi access point
IPv4 address acquired
Connecting to MQTT broker 192.168.0.66:1883
Connected to MQTT broker
Wi-Fi RSSI: -XX dBm
```

## 3. Home Assistant 확인

Home Assistant에서 MQTT integration이 Mosquitto에 연결되어 있어야 합니다.
보드가 MQTT에 연결되면 discovery 메시지를 보내므로 `설정 → 장치 및 서비스 →
MQTT` 아래에 `Bedroom Fan Controller` 장치가 자동으로 나타납니다.

이 프로젝트에서 사용하는 토픽은 다음과 같습니다.

```text
wifi_fan/power/set
wifi_fan/power/state
wifi_fan/speed/set
wifi_fan/speed/state
wifi_fan/oscillation/set
wifi_fan/oscillation/state
wifi_fan/availability
wifi_fan/wifi_rssi/state
```

- 명령 메시지는 retain하지 않습니다.
- 보드가 받은 retained 명령은 안전을 위해 무시합니다.
- 현재 상태, RSSI, discovery 정보는 retain합니다.
- MQTT 연결이 비정상적으로 끊기면 Last Will이 `offline`을 게시합니다.
- 보드 재부팅 시 선풍기가 자동으로 다시 켜지지 않고 OFF에서 시작합니다.

## RSSI 해석

환경에 따라 달라지지만 대략 다음 기준으로 볼 수 있습니다.

| RSSI | 상태 |
|---:|---|
| -30 ~ -50 dBm | 매우 강함 |
| -50 ~ -60 dBm | 안정적 |
| -60 ~ -70 dBm | 일반적으로 사용 가능 |
| -70 ~ -80 dBm | 약함, 재연결 가능성 증가 |
| -80 dBm 이하 | 불안정할 가능성이 큼 |

## 전기 안전

릴레이 접점이 상용 전압이나 모터 권선을 제어한다면 ESP32 회로와 선풍기 회로를
공통 GND로 연결하지 마십시오. 모터 부하에 맞는 릴레이 접점 정격, 절연거리,
퓨즈와 난연 케이스가 필요합니다. 전원이 완전히 분리된 상태에서 배선하십시오.
