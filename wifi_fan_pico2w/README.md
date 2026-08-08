# Pico 2 W Wi-Fi MQTT Fan Controller

Raspberry Pi Pico 2 W와 Zephyr를 사용해 active-high 릴레이 네 개로 3단
기계식 선풍기를 제어하는 프로젝트입니다. 기존 `wifi_fan` ESP32-S3 제어기를
대체하면서 Home Assistant의 기존 선풍기 장치와 엔티티를 그대로 이어받습니다.

## 하드웨어 연결

| 기능 | Pico 2 W 핀 | 릴레이 동작 |
|---|---:|---|
| 풍속 1 | GP4 | High인 동안 ON |
| 풍속 2 | GP5 | High인 동안 ON |
| 풍속 3 | GP6 | High인 동안 ON |
| 회전 | GP7 | High인 동안 ON |

풍속을 바꿀 때 세 풍속 릴레이를 모두 끈 뒤 100ms 후 선택한 릴레이 하나만
켭니다. 부팅 시 모든 릴레이는 OFF입니다. Pico 2 W의 Wi-Fi가 내부적으로
사용하는 GP23, GP24, GP25, GP29는 릴레이에 사용하지 않습니다.

Pico 2 W의 GPIO는 3.3V입니다. 릴레이 모듈 입력이 3.3V High를 인식하는지
확인하고, 릴레이 코일 전류를 GPIO에서 직접 공급하지 마십시오.

## 프로젝트 설정 상태

- 보드: `rpi_pico2/rp2350a/m33/w`
- Wi-Fi: CYW43439 / Infineon AIROC
- Wi-Fi 국가 설정: 대한민국
- MQTT: TLS 8883, 사용자명/비밀번호 인증
- 서버 검증: `certs/fan-ca.crt` 공개 CA를 펌웨어에 포함
- Home Assistant: MQTT Discovery
- RSSI: 30초마다 MQTT로 보고
- CYW43439 확인: Wi-Fi 초기화 후 내장 LED를 계속 ON
- CYW43439 리소스: Raspberry Pi Pico SDK와 일치하는 firmware/CLM/NVRAM 세트

Pico 2 W의 내장 LED는 RP2350 GPIO가 아니라 CYW43439 GPIO0에 연결되어
있습니다. 부팅 시 Wi-Fi 인터페이스 생성에 성공한 뒤 LED를 켭니다. LED가
켜지고 다음 로그가 나오면 WHD 드라이버가 CYW43439의 GPIO까지 실제로 제어한
것입니다.

```text
CYW43439 onboard LED ON using gpioout mask/value payload
```

CYW43439 초기화가 실패하면 LED 제어를 시도하지 않고 다음 로그를 남긴 뒤
시작을 중단합니다.

```text
CYW43439 unavailable; onboard LED test cannot run
```

현재 `wifi_fan_private.conf`의 MQTT 사용자명과 비밀번호는 정상 동작하는 기존
ESP32 프로젝트 계정과 일치시켰습니다. Wi-Fi SSID와 비밀번호는 Pico 2 W가
설치될 네트워크 값으로 별도 관리합니다. 이 파일은 `.gitignore`로 Git에서
제외됩니다. 별도 MQTT 계정을 만들려면 다음 값을 새 계정으로 바꿉니다.

```conf
CONFIG_WIFI_FAN_MQTT_USERNAME="wifi_fan_pico2w"
CONFIG_WIFI_FAN_MQTT_PASSWORD="새 MQTT 비밀번호"
```

## MQTT와 Home Assistant 식별자

기존 Home Assistant 장치를 이어받기 위해 ESP32 프로젝트와 같은 토픽을
사용합니다.

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

MQTT client ID는 브로커 세션 구분을 위해 `wifi_fan_pico2w`를 유지합니다.
Home Assistant의 unique ID와 device identifier는 기존 ESP32 값을 사용하므로
`Bedroom Fan Controller` 장치의 구현 보드만 Pico 2 W로 갱신됩니다. 기존 ESP32와
Pico 2 W를 동시에 켜면 같은 명령·상태 토픽을 사용해 충돌하므로 함께 운용하면
안 됩니다.

Mosquitto에서 ACL을 사용하고 있다면 Pico 장치가 위 토픽과 Discovery 토픽에
접근할 수 있도록 권한을 추가해야 합니다. 별도 `wifi_fan_pico2w` 사용자를
만드는 경우의 예는 다음과 같습니다.

```conf
user wifi_fan_pico2w
topic read wifi_fan/power/set
topic read wifi_fan/speed/set
topic read wifi_fan/oscillation/set
topic write wifi_fan/power/state
topic write wifi_fan/speed/state
topic write wifi_fan/oscillation/state
topic write wifi_fan/wifi_rssi/state
topic write wifi_fan/availability
topic write homeassistant/fan/wifi_fan_esp32s3/#
topic write homeassistant/sensor/wifi_fan_esp32s3/#
```

기존 `wifi_fan_device` 계정을 계속 쓴다면 위 권한을 그 사용자의 ACL 아래에
추가해야 합니다. ACL 변경은 컴파일과 무관하지만 실제 MQTT 연결 및 Home
Assistant 동작 전에는 필요합니다.

## Pico 2 W Wi-Fi 리소스

Zephyr의 기본 CYW43439 설정은 일반 Murata 1YN firmware/CLM/NVRAM 조합을
선택합니다. Pico 2 W에서는 이 조합으로 펌웨어가 WLAN 인터페이스를 UP 상태로
전환하지 못했습니다. 이 프로젝트는 Raspberry Pi Pico SDK 2.2.0과 일치하는
세 파일을 `resources/`에서 함께 사용합니다.

```text
resources/pico2w-cyw43439-firmware.bin
resources/pico2w-cyw43439.clm_blob
resources/pico2w-cyw43439-nvram.txt
```

세 파일은 서로 호환되는 한 세트이므로 일부만 일반 Infineon 리소스로 바꾸면
안 됩니다. 출처와 재생성 방법은 `resources/README.md`에 정리되어 있습니다.

## 컴파일 명령

컴파일은 사용자가 확인한 뒤 실행합니다. ESP32용 flash/PSRAM snippet은 넣지
않습니다.

```sh
cd /Volumes/ej_disk/zephyrproject

.venv/bin/west build -p always \
  -b rpi_pico2/rp2350a/m33/w \
  -d EJ_APP/build_pico2w_fan \
  EJ_APP/wifi_fan_pico2w \
  -- -DEXTRA_CONF_FILE=wifi_fan_private.conf
```

성공하면 UF2 파일은 다음 위치에 생성됩니다.

```text
EJ_APP/build_pico2w_fan/zephyr/zephyr.uf2
```

Pico 2 W의 BOOTSEL 버튼을 누른 채 USB를 연결하고 나타나는 저장장치에 UF2를
복사하면 됩니다.

## 로그 확인

로그는 UART0와 SEGGER RTT 양쪽으로 출력됩니다.

- Pico GP0(TX) → USB-UART 어댑터 RX
- Pico GP1(RX) → USB-UART 어댑터 TX
- Pico GND → USB-UART 어댑터 GND
- 속도: 115200 baud, 8-N-1

Pico USB 포트는 기본 설정에서 자동 USB 시리얼 콘솔이 아닙니다. USB-UART
어댑터 없이 USB CDC 로그를 사용하려면 별도 콘솔 설정 변경이 필요합니다.

Pico Debug의 SWD 포트로 Flash한 경우에는 별도 UART 배선 없이 RTT로 로그를
확인할 수 있습니다. RP2350을 지원하는 Raspberry Pi OpenOCD 경로를 지정합니다.

```sh
cd /Volumes/ej_disk/zephyrproject

west rtt \
  -d EJ_APP/build_pico2w_fan \
  -r openocd \
  --openocd /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/openocd \
  --openocd-search /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/scripts
```

RTT는 SWD의 RAM 접근을 통해 로그를 읽습니다. monitor가 연결되지 않아도 제어
동작이 로그 출력 때문에 멈추지 않도록 RTT backend는 drop mode로 설정했습니다.

정상적인 로그 순서는 다음과 같습니다.

```text
Pico 2 W Wi-Fi fan controller start
All active-high relays initialized OFF
MQTT CA certificate registered
Connecting to Wi-Fi SSID '...'
Connected to Wi-Fi access point
IPv4 address acquired
System time synchronized
Resolved MQTT host '...'
Connected to MQTT broker
MQTT command topics subscribed
```

`MQTT login rejected`가 나오면 TLS 연결까지는 성공했고 Mosquitto가 계정 또는
권한을 거부한 것입니다. `result 5`는 MQTT 3.1.1의 `Not authorized`이며 Wi-Fi나
TLS 인증서 문제가 아닙니다.

## 해결한 Pico 2 W 호환 문제

1. 일반 CYW43439 리소스와 Pico용 NVRAM이 섞이면 `WHD_WLAN_NOTUP` 상태가
   지속되어 스캔과 접속이 모두 실패했습니다. firmware/CLM/NVRAM 전체를 Pico
   SDK 조합으로 통일해 해결했습니다.
2. Zephyr CYW43 GPIO 드라이버의 `gpioout` 쓰기는 값 하나만 전달하지만 Pico의
   LED 제어에는 mask와 value 두 값이 필요했습니다. `status_led.c`가 완전한
   payload를 보내도록 우회했습니다.
3. 원인 확인용 WHD 함수 래퍼, IOVAR 추적, 인증 이벤트 추적, AP 사전 스캔은
   최종 코드에서 제거했습니다.
4. RTT monitor가 없어도 로그 버퍼 때문에 제어가 멈추지 않도록 drop mode를
   사용합니다.

## 안전

상용 전압과 모터 배선 작업은 반드시 전원을 완전히 분리한 상태에서 수행해야
합니다. 모터 부하에 맞는 릴레이 정격, 절연거리, 퓨즈 및 난연 케이스를
사용하십시오.
