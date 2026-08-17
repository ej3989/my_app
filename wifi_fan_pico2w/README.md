# Pico 2 W Wi-Fi MQTT Fan Controller

Raspberry Pi Pico 2 W와 Zephyr를 사용해 active-low 릴레이 네 개로 3단
기계식 선풍기를 제어하는 프로젝트입니다. 기존 `wifi_fan` ESP32-S3 제어기를
대체하면서 Home Assistant의 기존 선풍기 장치와 엔티티를 그대로 이어받습니다.

## 하드웨어 연결

| 기능 | Pico 2 W 핀 | GPIO 출력 | 릴레이 상태 |
|---|---:|---:|---|
| 풍속 1 | GP4 | Low | ON |
| 풍속 2 | GP5 | Low | ON |
| 풍속 3 | GP6 | Low | ON |
| 회전 | GP7 | Low | ON |

풍속을 바꿀 때 세 풍속 릴레이를 모두 끈 뒤 100ms 후 선택한 릴레이 하나만
켭니다. 부팅 시 모든 릴레이는 OFF입니다. Pico 2 W의 Wi-Fi가 내부적으로
사용하는 GP23, GP24, GP25, GP29는 릴레이에 사용하지 않습니다.

Pico 2 W의 GPIO는 3.3V입니다. 릴레이 모듈은 active-low이므로 High가 OFF이고
Low가 ON입니다. 릴레이 코일 전류를 GPIO에서 직접 공급하지 마십시오.

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
- 복구: task watchdog과 RP2350 hardware watchdog 사용

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

## 선택형 OTA 구성

검증된 aging 보드와 같은 MCUboot/MCUmgr UDP OTA 구성이 포함되어 있습니다.
일반 빌드는 OTA를 포함하지 않으며, OTA 빌드는 다음 파일들을 추가로 사용합니다.

- `VERSION`: 애플리케이션 및 MCUboot 이미지 버전
- `sysbuild.conf`: 애플리케이션과 MCUboot를 함께 빌드
- `ota-mcumgr-udp.conf`: 같은 LAN의 UDP 1337 MCUmgr 서버
- `..._w_mcuboot.conf`, `.overlay`: MCUboot 보드에서도 Pico 전용 CYW43439와
  active-low 릴레이 설정 유지
- `scripts/ota_udp.py`: probe/upload/test/reset/confirm 도구

OTA용 빌드 명령은 다음과 같습니다.

```sh
cd /Volumes/ej_disk/zephyrproject

.venv/bin/west build -p always --sysbuild \
  -b rpi_pico2/rp2350a/m33/w/mcuboot \
  -d EJ_APP/build_pico2w_fan_ota \
  EJ_APP/wifi_fan_pico2w \
  -- -DEXTRA_CONF_FILE="wifi_fan_private.conf;ota-mcumgr-udp.conf"
```

현재 원본 보드에 MCUboot가 아직 없다면 최초 한 번은 전체 sysbuild 결과를
Pico Debug/OpenOCD로 물리 플래시해야 합니다.

```sh
west flash \
  -d EJ_APP/build_pico2w_fan_ota \
  -r openocd \
  --openocd /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/openocd \
  --openocd-search /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/scripts
```

부팅 후 다음 로그가 있어야 OTA 서버가 준비된 것입니다. 서버는 system init에서
자동으로 열지 않고 DHCP가 실제 IPv4 주소를 배정한 뒤 명시적으로 시작합니다.

```text
IPv4 address acquired: 192.168.x.x
MCUmgr OTA server listening on UDP port 1337
```

OTA 도구 환경은 다음처럼 한 번 준비합니다.

```sh
cd /Volumes/ej_disk/zephyrproject
.venv/bin/python -m venv EJ_APP/wifi_fan_pico2w/.ota-venv
EJ_APP/wifi_fan_pico2w/.ota-venv/bin/python -m pip install \
  -r EJ_APP/wifi_fan_pico2w/ota-requirements.txt

OTA_PY=EJ_APP/wifi_fan_pico2w/.ota-venv/bin/python
OTA_TOOL=EJ_APP/wifi_fan_pico2w/scripts/ota_udp.py
```

현재 상태 확인과 다음 버전 업로드 순서는 다음과 같습니다.

```sh
$OTA_PY $OTA_TOOL <PICO_IP> probe

$OTA_PY $OTA_TOOL <PICO_IP> upload \
  EJ_APP/build_pico2w_fan_ota/wifi_fan_pico2w/zephyr/zephyr.signed.bin \
  --timeout 30 --retries 10

$OTA_PY $OTA_TOOL <PICO_IP> test
$OTA_PY $OTA_TOOL <PICO_IP> reset
```

UDP 응답이 유실되면 업로드 도구가 재접속하고 보드가 기억한 offset부터 이어서
전송합니다. 새 시험 이미지는 Wi-Fi, MQTT TLS, Discovery와 최초 상태 발행까지
성공한 뒤 자동으로 confirm됩니다. 재부팅 후 다음 결과가 최종 성공 상태입니다.

```text
slot  version  active  pending  confirmed  bootable
0     x.y.z    True    False    True       True
```

새 버전을 만들 때는 `VERSION`을 먼저 증가시켜야 합니다. UDP 1337은 인증이나
암호화가 없는 같은 LAN 전용 관리 포트이므로 공유기 포트 포워딩을 하면 안 됩니다.
MQTT TLS의 `fan-ca.crt`와 MCUboot 이미지 서명키는 서로 다른 용도입니다.

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

## 자동 복구와 왓치독

버전 1.0.2부터 두 종류의 task watchdog 채널과 RP2350 하드웨어 왓치독을 함께
사용합니다. 버전 1.0.3에서는 실제 선풍기의 복구 시간을 줄이기 위해 네트워크
왓치독을 10분에서 3분으로 변경했습니다.

- 애플리케이션 채널: 메인 제어 흐름과 MQTT 처리 루프가 120초 동안 진행하지
  못하면 재부팅합니다.
- 네트워크 채널: Wi-Fi 연결, MQTT TLS 로그인, 구독 및 최초 상태 발행까지 성공한
  정상 MQTT 세션이 180초 동안 한 번도 유지되지 않으면 재부팅합니다.
- 하드웨어 fallback: 커널 타이머나 인터럽트 처리 자체가 멈추면 약 15초 후
  RP2350 하드웨어가 SoC를 리셋합니다.

단순한 일시적 Wi-Fi 또는 MQTT 끊김은 기존 재접속 루프가 먼저 처리합니다. 3분
네트워크 왓치독은 드라이버나 네트워크 상태가 장시간 복구되지 않는 경우에만 전원
재인가와 비슷한 cold reboot를 수행하기 위한 마지막 복구 단계입니다. 따라서 공유기
장애가 계속되면 보드는 약 3분 간격으로 재부팅한 뒤 다시 연결을 시도합니다.

OpenOCD가 CPU를 halt한 동안에는 RP2350 하드웨어 왓치독이 정지하도록 설정되므로
정상적인 디버깅 때문에 보드가 리셋되지는 않습니다. 다음 부팅에서 reset cause가
남아 있으면 로그로 원인을 구분할 수 있습니다.

```text
Watchdogs armed: application 120 s, network 180 s, hardware fallback 15 s
Previous reset was caused by the RP2350 hardware watchdog
Previous reset included a brownout condition
```

첫 번째 줄은 정상적으로 보호 기능이 시작됐다는 뜻입니다. 뒤 두 줄은 각각 이전
부팅이 하드웨어 왓치독 또는 저전압에 의해 끝났음을 뜻하며, 해당 원인이 없으면
표시되지 않습니다. task watchdog이 직접 cold reboot한 경우 플랫폼 reset-cause에
항상 watchdog으로 기록되는 것은 아니므로, 재부팅 직전 RTT에는
`Application watchdog expired` 또는 `Network unavailable for 180 seconds`가
출력됩니다.

정상적인 로그 순서는 다음과 같습니다.

```text
Pico 2 W Wi-Fi fan controller v1.0.3 start
All active-low relays initialized OFF (GPIO HIGH)
MQTT CA certificate registered
Watchdogs armed: application 120 s, network 180 s, hardware fallback 15 s
Connecting to Wi-Fi SSID '...'
Connected to Wi-Fi access point
IPv4 address acquired: 192.168.x.x
MCUmgr OTA server listening on UDP port 1337
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
