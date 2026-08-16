# Pico 2 W Wi-Fi Fan Aging Controller

기존 `wifi_fan_pico2w`를 그대로 운용하면서 두 번째 Raspberry Pi Pico 2 W로
장시간 연결 안정성을 시험하기 위한 독립 프로젝트입니다. MQTT client ID,
Home Assistant device identifier, entity unique ID와 모든 상태/명령 토픽을
`aging01` 전용 값으로 분리했으므로 두 보드를 동시에 켜도 충돌하지 않습니다.

## 하드웨어

| 기능 | Pico 2 W 핀 | GPIO 출력 | 릴레이 상태 |
|---|---:|---:|---|
| 풍속 1 | GP4 | Low | ON |
| 풍속 2 | GP5 | Low | ON |
| 풍속 3 | GP6 | Low | ON |
| 회전 | GP7 | Low | ON |

릴레이 모듈은 active-low입니다. High가 OFF이고 Low가 ON입니다. 부팅 시 네
릴레이를 모두 OFF로 초기화합니다. 풍속 변경 시 세 풍속 릴레이를 모두 끄고
100 ms 뒤 선택한 릴레이 하나만 켭니다. 회전 릴레이는 풍속과 독립적으로
상태를 유지합니다.

Pico 2 W GPIO는 3.3 V이므로 릴레이 코일을 GPIO에서 직접 구동하지 말고,
3.3 V 입력을 인식하는 절연 릴레이 모듈과 공통 GND를 사용하십시오.

## 기존 장치와 분리된 식별자

- MQTT client ID: `wifi_fan_pico2w_aging01`
- Home Assistant device identifier: `wifi_fan_pico2w_aging01`
- 표시 이름: `Pico Fan Aging Controller 01`
- Fan entity unique ID: `wifi_fan_pico2w_aging01_fan`
- RSSI entity unique ID: `wifi_fan_pico2w_aging01_rssi`

전용 MQTT 토픽은 다음과 같습니다.

```text
wifi_fan/aging01/power/set
wifi_fan/aging01/power/state
wifi_fan/aging01/speed/set
wifi_fan/aging01/speed/state
wifi_fan/aging01/oscillation/set
wifi_fan/aging01/oscillation/state
wifi_fan/aging01/availability
wifi_fan/aging01/wifi_rssi/state
```

Home Assistant Discovery 토픽도 다음처럼 분리됩니다.

```text
homeassistant/fan/wifi_fan_pico2w_aging01/fan/config
homeassistant/sensor/wifi_fan_pico2w_aging01/rssi/config
```

따라서 기존 `Bedroom Fan Controller`가 아니라 새로운 장치로 등록됩니다.
온라인 상태는 retained availability 메시지와 MQTT Last Will로 판단하고,
RSSI는 30초마다 보고합니다. 장시간 시험 중 `사용할 수 없음`이 나타난 시각과
RSSI 이력을 Home Assistant에서 확인할 수 있습니다.

## 자동 복구와 왓치독

버전 1.0.2부터 장시간 에이징 중 멈춤과 장기 네트워크 불능을 자동 복구합니다.

- 메인/MQTT 처리 흐름이 120초 동안 진행하지 못하면 task watchdog이 cold reboot
  합니다.
- 정상 MQTT 세션이 600초 동안 한 번도 유지되지 않으면 네트워크 watchdog이 cold
  reboot합니다. 짧은 끊김에는 기존 Wi-Fi/MQTT 재접속이 먼저 동작합니다.
- 커널 타이머 자체가 멈추는 더 심한 고장은 약 15초 RP2350 hardware watchdog이
  SoC reset으로 복구합니다.

```text
Watchdogs armed: application 120 s, network 600 s, hardware fallback 15 s
Application watchdog expired; rebooting
Network unavailable for 600 seconds; rebooting
Previous reset was caused by the RP2350 hardware watchdog
Previous reset included a brownout condition
```

첫 줄은 보호 기능이 정상 시작된 로그입니다. 나머지는 실제로 발생한 원인만
표시됩니다. OpenOCD가 CPU를 halt한 동안에는 hardware watchdog도 정지하므로 RTT/GDB
디버깅이 리셋을 유발하지 않습니다. 공유기나 MQTT 서버가 10분 이상 계속 중단되면
보드는 약 10분마다 재부팅하며 복구를 다시 시도합니다.

## 비공개 설정

`wifi_fan_private.conf.example`을 `wifi_fan_private.conf`로 복사하고 실제 값을
입력합니다. 복사된 현재 파일에는 기존 설정이 들어 있지만 Git에서는 제외됩니다.
CA 공개 인증서 `certs/fan-ca.crt`는 기존 Mosquitto 서버 인증서를 검증하는
공개 신뢰 정보이므로 여러 장치가 같이 사용해도 됩니다. MQTT 비밀번호는
공개 인증서가 아니며 저장소에 커밋하면 안 됩니다.

동일한 Mosquitto 사용자 계정을 재사용할 수 있지만, 장치별 계정을 만들면 장애와
권한을 더 쉽게 구분할 수 있습니다. 어느 쪽이든 ACL을 쓴다면 다음 권한이
필요합니다.

```conf
user wifi_fan_aging01
topic read wifi_fan/aging01/power/set
topic read wifi_fan/aging01/speed/set
topic read wifi_fan/aging01/oscillation/set
topic write wifi_fan/aging01/power/state
topic write wifi_fan/aging01/speed/state
topic write wifi_fan/aging01/oscillation/state
topic write wifi_fan/aging01/wifi_rssi/state
topic write wifi_fan/aging01/availability
topic write homeassistant/fan/wifi_fan_pico2w_aging01/#
topic write homeassistant/sensor/wifi_fan_pico2w_aging01/#
```

## CYW43439 구성

이 프로젝트는 정상 동작이 확인된 Pico 2 W 전용 firmware/CLM/NVRAM 세트를
`resources/`에 유지합니다. 일반 Murata 1YN 리소스로 되돌리지 마십시오.

```text
resources/pico2w-cyw43439-firmware.bin
resources/pico2w-cyw43439.clm_blob
resources/pico2w-cyw43439-nvram.txt
```

내장 LED는 RP2350 GPIO가 아니라 CYW43439 GPIO0입니다. 부팅 후 LED가 켜지고
아래 로그가 나오면 Wi-Fi 칩과 GPIO 통신이 성공한 것입니다.

```text
CYW43439 onboard LED ON using gpioout mask/value payload
```

## 일반 빌드

일반 빌드는 기존 방식과 같고 OTA 기능을 포함하지 않습니다. 컴파일은 사용자가
확인한 뒤 실행합니다.

```sh
cd /Volumes/ej_disk/zephyrproject

.venv/bin/west build -p always \
  -b rpi_pico2/rp2350a/m33/w \
  -d EJ_APP/build_pico2w_fan_aging \
  EJ_APP/wifi_fan_pico2w_aging \
  -- -DEXTRA_CONF_FILE=wifi_fan_private.conf
```

## 선택형 OTA 구성

OTA는 가능합니다. 이 프로젝트에는 다음 요소를 미리 추가했습니다.

- `sysbuild.conf`: 애플리케이션과 MCUboot 부트로더를 함께 빌드
- `ota-mcumgr-udp.conf`: MCUmgr/SMP 이미지 업로드와 UDP 1337 활성화
- `..._w_mcuboot.conf`와 `.overlay`: MCUboot 보드 변형에서도 Pico용 Wi-Fi와
  active-low 릴레이 설정 유지
- Pico 2 W MCUboot 보드의 4 MB 파티션: bootloader, slot 0, slot 1, storage

현재 Zephyr의 Pico 2 W MCUboot DTS에는 일반 Pico 2 W DTS에 있는
`cyw43_gpio` 자식 노드가 빠져 있습니다. MCUboot 전용 애플리케이션 오버레이가
같은 노드를 보완하여 내장 LED 설정도 일반 빌드와 동일하게 유지합니다.

slot 0에는 실행 중인 이미지, slot 1에는 업로드할 새 이미지를 둡니다. MCUboot는
서명 검증을 통과한 새 이미지를 시험 부팅하고, 새 펌웨어가 확인되지 않으면 이전
이미지로 되돌릴 수 있습니다.

OTA 시험용 빌드는 다음 명령을 사용합니다. `--sysbuild`와 보드명의 `/mcuboot`가
둘 다 필요합니다.

```sh
cd /Volumes/ej_disk/zephyrproject

.venv/bin/west build -p always --sysbuild \
  -b rpi_pico2/rp2350a/m33/w/mcuboot \
  -d EJ_APP/build_pico2w_fan_aging_ota \
  EJ_APP/wifi_fan_pico2w_aging \
  -- -DEXTRA_CONF_FILE="wifi_fan_private.conf;ota-mcumgr-udp.conf"
```

첫 설치는 빈 보드에 MCUboot도 넣어야 하므로 OTA가 아니라 Pico Debug/OpenOCD로
전체 sysbuild 결과를 flash해야 합니다.

```sh
cd /Volumes/ej_disk/zephyrproject

west flash \
  -d EJ_APP/build_pico2w_fan_aging_ota \
  -r openocd \
  --openocd /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/openocd \
  --openocd-search /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/scripts
```

그 다음 버전부터는 빌드 결과의 `zephyr.signed.bin`을 MCUmgr 클라이언트로
Pico의 LAN IP와 UDP 1337에 업로드하고, test 지정, reset, 정상 동작 확인,
confirm 순서로 적용합니다. 정확한 산출물 위치는 빌드 후 다음으로 찾을 수
있습니다.

초기 OTA 펌웨어에서 UDP transport의 자동 시작이 DHCP보다 먼저 실행되면 Wi-Fi와
MQTT가 정상이어도 UDP 1337이 열리지 않을 수 있습니다. 현재 구성은 자동 시작을
끄고, 애플리케이션이 IPv4 주소 획득을 확인한 직후 `smp_udp_open()`을 호출합니다.
다음 로그가 있어야 OTA 요청을 받을 준비가 된 것입니다.

```text
MCUmgr OTA server listening on UDP port 1337
```

이 수정이 들어가기 전에 설치한 펌웨어가 `Connection refused`를 반환한다면 OTA로
자기 자신을 고칠 수 없으므로, 현재 수정본을 OpenOCD로 한 번 더 물리 플래시해야
합니다. 그 이후의 버전부터 OTA 업로드를 사용할 수 있습니다.

```sh
find EJ_APP/build_pico2w_fan_aging_ota -name zephyr.signed.bin -print
```

프로젝트 전용 `.ota-venv`에는 UDP를 지원하는 `smpclient` 7.1.0을 설치했습니다.
다른 환경에서 다시 만들려면 다음을 실행합니다.

```sh
cd /Volumes/ej_disk/zephyrproject
.venv/bin/python -m venv EJ_APP/wifi_fan_pico2w_aging/.ota-venv
EJ_APP/wifi_fan_pico2w_aging/.ota-venv/bin/python -m pip install \
  -r EJ_APP/wifi_fan_pico2w_aging/ota-requirements.txt
```

`scripts/ota_udp.py`는 `probe`, `list`, `upload`, `test`, `reset`, `confirm`
명령을 제공합니다. Mac과 Pico가 같은 LAN에 있어야 하며 `<PICO_IP>`는 공유기
DHCP 목록에서 확인합니다. 새 펌웨어부터는 부팅 로그에도 실제 IPv4 주소가
표시됩니다.

먼저 OTA 서버 응답과 현재 slot 상태를 확인합니다.

```sh
OTA_PY=EJ_APP/wifi_fan_pico2w_aging/.ota-venv/bin/python
OTA_TOOL=EJ_APP/wifi_fan_pico2w_aging/scripts/ota_udp.py

$OTA_PY $OTA_TOOL <PICO_IP> probe
```

새 서명 이미지를 slot 1에 업로드합니다. 이 단계만으로는 아직 재부팅하거나
새 이미지를 실행하지 않습니다.

```sh
$OTA_PY $OTA_TOOL <PICO_IP> upload \
  EJ_APP/build_pico2w_fan_aging_ota/wifi_fan_pico2w_aging/zephyr/zephyr.signed.bin
```

UDP 응답 하나가 유실되거나 플래시 쓰기가 요청 제한시간보다 오래 걸리면 업로드
도구는 새 UDP 연결을 만들고 동일 이미지의 SHA-256을 보내 서버가 보관한 offset부터
자동으로 이어서 전송합니다. 기본값은 요청당 10초와 최대 5회 재시도입니다. 불안정한
Wi-Fi에서는 다음처럼 제한시간과 재시도 횟수를 늘릴 수 있습니다.

```sh
$OTA_PY $OTA_TOOL <PICO_IP> upload \
  EJ_APP/build_pico2w_fan_aging_ota/wifi_fan_pico2w_aging/zephyr/zephyr.signed.bin \
  --timeout 30 --retries 10
```

업로드한 비활성 이미지를 한 번만 시험 부팅하도록 지정하고 재부팅합니다.

```sh
$OTA_PY $OTA_TOOL <PICO_IP> test
$OTA_PY $OTA_TOOL <PICO_IP> reset
```

버전 1.0.1부터는 Wi-Fi, MQTT TLS, Discovery 및 최초 상태 발행이 모두 성공한
후 다음 로그를 남기고 스스로 confirm합니다.

```text
Pico 2 W Wi-Fi fan aging controller v1.0.1 start
IPv4 address acquired: 192.168.x.x
MCUmgr OTA server listening on UDP port 1337
MCUboot OTA image confirmed after MQTT startup
```

재접속 후 상태를 확인합니다.

```sh
$OTA_PY $OTA_TOOL <PICO_IP> list
```

새 active slot이 `confirmed=True`이면 OTA 전체 과정이 성공한 것입니다. 자동
confirm 전에 실패한 시험 이미지는 다음 reset에서 이전 정상 이미지로 되돌아갑니다.
자동 confirm을 사용하지 않는 펌웨어를 시험할 때만 다음 수동 명령을 사용합니다.

```sh
$OTA_PY $OTA_TOOL <PICO_IP> confirm
```

### 확인된 최초 OTA 빌드

2026-08-15에 sysbuild 구성이 정상 완료됐습니다.

```text
application: 497,928 / 847,536 bytes (58.75%)
application RAM: 194,280 / 532,480 bytes (36.49%)
MCUboot: 34,872 / 65,536 bytes (53.21%)
OTA signed image: 498,264 bytes
```

최초 물리 플래시 순서는 sysbuild의 `domains.yaml`에 `mcuboot` 다음
`wifi_fan_pico2w_aging`으로 생성됐습니다. `west flash`를 최상위 sysbuild
디렉터리에 실행하면 이 순서를 사용합니다.

### OTA 보안 범위

현재 `ota-mcumgr-udp.conf`는 기능 확인을 위한 같은 LAN 전용 구성입니다. UDP
1337을 공유기에서 포트 포워딩하면 안 됩니다. 전송 채널 자체에는 인증과 암호화가
없지만, MCUboot는 서명된 펌웨어만 부팅하는 별도의 검증 계층을 제공합니다.

초기 시험 빌드는 Zephyr/MCUboot의 개발용 기본 서명키를 사용할 수 있습니다.
이 키는 공개된 개발키이므로 제품용 보안키가 아닙니다. 실제 운용 전에는 개인
서명키를 생성해 안전한 PC에 보관하고, 그 공개키를 포함한 MCUboot를 보드에
물리적으로 다시 설치해야 합니다. 인터넷을 통한 원격 OTA는 raw UDP 대신
DTLS를 추가하거나, 장치가 HTTPS 서버를 주기적으로 확인하는 hawkBit 같은
pull 방식으로 별도 설계하는 편이 안전합니다.

기존 MQTT TLS의 `fan-ca.crt`는 MQTT 서버 신원 검증용이며 MCUboot 펌웨어
서명키와 목적이 완전히 다릅니다. OTA 서명키 대신 재사용하지 않습니다.

## 로그 확인

일반 빌드와 OTA 빌드 모두 RTT 설정을 유지합니다. 해당 빌드 디렉터리를 `-d`에
지정합니다.

```sh
west rtt \
  -d EJ_APP/build_pico2w_fan_aging_ota \
  -r openocd \
  --openocd /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/openocd \
  --openocd-search /Users/jaeheelee/.pico-sdk/openocd/0.12.0+dev/scripts
```

## 안전

상용 전압과 모터 배선은 전원을 완전히 분리한 뒤 작업하십시오. 모터 부하에 맞는
릴레이 정격, 절연거리, 퓨즈와 난연 케이스를 사용하십시오.
