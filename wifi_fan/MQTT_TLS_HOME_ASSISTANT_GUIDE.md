# MQTT TLS + Home Assistant + Zephyr 재사용 가이드

이 문서는 Raspberry Pi의 Docker Mosquitto, Home Assistant, 외부 네트워크의
ESP32-S3 Zephyr 장치를 MQTT over TLS로 연결하는 전체 과정을 한 파일로 정리한
운영·복구·재사용 문서다.

현재 `wifi_fan` 프로젝트에서 실제로 사용한 구성을 기준으로 작성했지만, 토픽,
장치 ID, GPIO 제어 부분을 바꾸면 다른 MQTT 장치 프로젝트에도 그대로 적용할 수
있다.

> 이 문서에는 비밀번호와 개인키 내용이 없다. `MQTT_PASSWORD`, Wi-Fi 비밀번호,
> `fan-ca.key`, `mqtt-server.key`는 외부에 공유하거나 Git에 커밋하지 않는다.

---

## 1. 최종 구성

```text
Home Assistant (Raspberry Pi, host network)
        │
        │ local MQTT TCP 1883
        ▼
Mosquitto Docker ───────────────────────────────────┐
        ▲                                           │
        │ MQTT over TLS TCP 8883                    │ 같은 broker 안에서
        │                                           │ 모든 topic을 공유
집 공유기 TCP 8883 포트포워딩                       │
        ▲                                           │
        │ ej3989.iptime.org:8883                    │
        │                                           │
인터넷                                              │
        ▲                                           │
        │                                           │
외부 공유기/Wi-Fi ── ESP32-S3 wifi_fan ────────────┘
```

현재 주요 값은 다음과 같다.

| 항목 | 값 |
|---|---|
| Raspberry Pi 내부 IP | `192.168.0.66` |
| DDNS 이름 | `ej3989.iptime.org` |
| Home Assistant | `http://192.168.0.66:8123` |
| 내부 MQTT | TCP `1883`, TLS 없음 |
| 외부 MQTT | TCP `8883`, TLS 필수 |
| ESP32 MQTT 사용자 | `wifi_fan_device` |
| Mosquitto 컨테이너 | `mosquitto` |
| Home Assistant 컨테이너 | `homeassistant` |
| Mosquitto 디렉터리 | `/home/ej3989/docker/mosquitto` |
| ESP32 CA 파일 | `EJ_APP/wifi_fan/certs/fan-ca.crt` |

외부에는 `8883`만 공개한다. 평문 MQTT `1883`, Home Assistant `8123`, 인증서
검증용으로 사용했던 `80`은 인터넷에 포트포워딩하지 않는다.

---

## 2. 각 구성요소의 역할

### Mosquitto

- MQTT 메시지 중계
- Home Assistant와 ESP32 사이의 command/state topic 전달
- TLS 서버 인증서 제시
- `wifi_fan_device` 사용자 인증
- ACL을 사용해 ESP32가 접근할 수 있는 topic 제한
- 연결이 끊겼을 때 Last Will 메시지 처리

### Home Assistant

- Mosquitto의 로컬 `1883` listener에 연결
- ESP32가 발행한 MQTT Discovery 메시지를 읽어 장치와 entity 생성
- 사용자의 전원·풍속·회전 명령을 command topic으로 발행
- ESP32가 발행한 state/availability/RSSI 표시

### ESP32-S3 Zephyr 펌웨어

- Wi-Fi 연결 및 DHCP
- DNS로 SNTP와 MQTT 호스트 이름 해석
- SNTP로 현재 UTC 시간 설정
- 펌웨어에 포함된 자체 루트 CA로 Mosquitto 인증서 검증
- `ej3989.iptime.org` 호스트 이름과 SNI 검증
- TLS 8883 연결 후 MQTT 사용자명·비밀번호 인증
- Home Assistant Discovery, 상태, RSSI 발행
- command topic을 받아 릴레이 GPIO 제어

### 자체 CA

- `fan-ca.key`: 서버 인증서를 서명하는 CA 개인키. 가장 중요한 비밀 파일
- `fan-ca.crt`: ESP32가 신뢰하는 공개 루트 CA 인증서
- `mqtt-server.key`: Mosquitto 서버 개인키
- `mqtt-server.crt`: `ej3989.iptime.org`용 Mosquitto 서버 인증서

---

## 3. Raspberry Pi Docker 구성

### 3.1 디렉터리 구조

```text
/home/ej3989/docker/mosquitto/
├── compose.yaml
├── ca-private/
│   ├── fan-ca.key
│   ├── fan-ca.srl
│   ├── mqtt-server.csr
│   └── mqtt-server.ext
├── config/
│   ├── mosquitto.conf
│   ├── password_file
│   ├── acl_file
│   └── certs/
│       ├── fan-ca.crt
│       ├── mqtt-server.crt
│       └── mqtt-server.key
├── data/
│   └── mosquitto.db
└── log/
    └── mosquitto.log
```

`ca-private`는 Mosquitto 컨테이너에 마운트하지 않는다. CA 개인키는 가능하면
암호화된 외부 저장장치에도 백업하고 평상시 서버에서 분리해 둔다.

### 3.2 `compose.yaml`

경로:

```text
/home/ej3989/docker/mosquitto/compose.yaml
```

내용:

```yaml
services:
  mosquitto:
    container_name: mosquitto
    image: eclipse-mosquitto:2

    volumes:
      - ./config:/mosquitto/config
      - ./data:/mosquitto/data
      - ./log:/mosquitto/log

    ports:
      - "1883:1883"
      - "8883:8883"

    restart: unless-stopped
```

확인 및 반영:

```bash
cd /home/ej3989/docker/mosquitto
docker compose config
docker compose up -d
```

포트 매핑을 변경했을 때는 단순 `docker restart`가 아니라
`docker compose up -d`로 컨테이너를 다시 생성해야 한다.

### 3.3 `mosquitto.conf`

경로:

```text
/home/ej3989/docker/mosquitto/config/mosquitto.conf
```

현재 동작 구성:

```conf
per_listener_settings true

persistence true
persistence_location /mosquitto/data/

log_dest file /mosquitto/log/mosquitto.log

# Home Assistant의 기존 로컬 연결
listener 1883
protocol mqtt
allow_anonymous true

# 외부 ESP32 TLS 연결
listener 8883
protocol mqtt

allow_anonymous false
password_file /mosquitto/config/password_file
acl_file /mosquitto/config/acl_file

certfile /mosquitto/config/certs/mqtt-server.crt
keyfile /mosquitto/config/certs/mqtt-server.key

tls_version tlsv1.2
require_certificate false
```

`require_certificate false`는 ESP32 클라이언트 인증서(mTLS)를 요구하지 않는다는
뜻이다. 서버 신원은 TLS 인증서로 검증하고 ESP32 신원은 MQTT 사용자명과
비밀번호로 검증한다.

현재 내부 1883은 Home Assistant 호환성을 유지하기 위해 익명 접속을 허용한다.
단, 공유기에서 1883을 포트포워딩하면 안 된다. 향후 내부 보안도 강화하려면
Home Assistant 전용 계정을 만든 뒤 1883도 `allow_anonymous false`로 변경한다.

Mosquitto 2.1부터 `per_listener_settings`, `password_file`, `acl_file` 방식이
deprecated로 표시될 수 있다. Mosquitto 2.x에서는 계속 동작하지만 향후 3.x로
업그레이드할 때는 listener별 authentication/ACL plugin 구성으로 이전해야 한다.
운영 중에는 이미지 버전을 확인하고 설정 백업 후 업그레이드한다.

버전 확인:

```bash
docker exec mosquitto mosquitto -h 2>&1 | head
```

---

## 4. 자체 CA와 Mosquitto 서버 인증서 만들기

### 4.1 자체 CA를 사용한 이유

`ej3989.iptime.org`에 Let’s Encrypt 인증서를 신청했을 때 다음 오류가 발생했다.

```text
Type: caa
CAA record for iptime.org prevents issuance
```

상위 도메인 `iptime.org`의 CAA 정책은 ipTIME이 관리하므로 사용자가 수정할 수
없다. 포트 80 설정 문제가 아니며 Certbot을 반복 실행해도 해결되지 않는다.

선택 가능한 방법은 다음과 같다.

1. 현재처럼 자체 CA를 만들고 ESP32에 공개 CA를 내장
2. DNS와 CAA를 직접 관리할 수 있는 개인 도메인 구매
3. HiveMQ Cloud 같은 외부 TLS MQTT broker 사용

ESP32 펌웨어를 직접 관리할 수 있는 현재 프로젝트에는 자체 CA가 적합하다.

### 4.2 CA 및 서버 인증서 생성

```bash
cd /home/ej3989/docker/mosquitto

mkdir -p ca-private
mkdir -p config/certs
chmod 700 ca-private
```

암호화된 CA 개인키 생성:

```bash
openssl genpkey \
  -algorithm RSA \
  -aes-256-cbc \
  -pkeyopt rsa_keygen_bits:3072 \
  -out ca-private/fan-ca.key
```

입력한 CA 개인키 암호는 안전하게 보관한다. 이 암호와 `fan-ca.key`가 없으면
같은 CA로 새 서버 인증서를 만들 수 없다.

루트 CA 인증서 생성:

```bash
openssl req \
  -x509 \
  -new \
  -sha256 \
  -days 3650 \
  -key ca-private/fan-ca.key \
  -out config/certs/fan-ca.crt \
  -subj "/C=KR/O=EJ Home/CN=EJ Home MQTT Root CA"
```

Mosquitto 서버 개인키 생성:

```bash
openssl genpkey \
  -algorithm RSA \
  -pkeyopt rsa_keygen_bits:2048 \
  -out config/certs/mqtt-server.key
```

CSR 생성:

```bash
openssl req \
  -new \
  -sha256 \
  -key config/certs/mqtt-server.key \
  -out ca-private/mqtt-server.csr \
  -subj "/C=KR/O=EJ Home/CN=ej3989.iptime.org"
```

확장 설정 파일 생성:

```bash
nano ca-private/mqtt-server.ext
```

```ini
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:ej3989.iptime.org
```

자체 CA로 서버 인증서 서명:

```bash
openssl x509 \
  -req \
  -in ca-private/mqtt-server.csr \
  -CA config/certs/fan-ca.crt \
  -CAkey ca-private/fan-ca.key \
  -CAserial ca-private/fan-ca.srl \
  -CAcreateserial \
  -out config/certs/mqtt-server.crt \
  -days 825 \
  -sha256 \
  -extfile ca-private/mqtt-server.ext
```

`subjectAltName=DNS:ej3989.iptime.org`가 반드시 있어야 한다. 최신 TLS 클라이언트는
CN만으로 호스트 이름을 검증하지 않는다.

### 4.3 인증서 검증

```bash
openssl verify \
  -CAfile config/certs/fan-ca.crt \
  config/certs/mqtt-server.crt
```

정상 결과:

```text
config/certs/mqtt-server.crt: OK
```

상세 정보 확인:

```bash
openssl x509 \
  -in config/certs/mqtt-server.crt \
  -noout -subject -issuer -dates -ext subjectAltName
```

현재 CA 정보:

```text
subject=C=KR, O=EJ Home, CN=EJ Home MQTT Root CA
issuer=C=KR, O=EJ Home, CN=EJ Home MQTT Root CA
notBefore=Aug  2 07:57:01 2026 GMT
notAfter=Jul 30 07:57:01 2036 GMT
SHA256=58:B2:7D:2D:E7:20:21:FA:9C:AA:79:59:6A:3E:59:88:
       6F:8B:47:18:1C:D8:E8:2C:41:EB:45:F0:FF:63:D9:4F
```

현재 서버 인증서 정보:

```text
subject=C=KR, O=EJ Home, CN=ej3989.iptime.org
issuer=C=KR, O=EJ Home, CN=EJ Home MQTT Root CA
notBefore=Aug  2 07:58:59 2026 GMT
notAfter=Nov  4 07:58:59 2028 GMT
SAN=DNS:ej3989.iptime.org
```

### 4.4 파일 권한

```bash
chmod 600 ca-private/fan-ca.key
chmod 600 config/certs/mqtt-server.key
chmod 644 config/certs/mqtt-server.crt
chmod 644 config/certs/fan-ca.crt

sudo chown 1883:1883 config/certs/mqtt-server.key
sudo chown 1883:1883 config/certs/mqtt-server.crt
sudo chown 1883:1883 config/certs/fan-ca.crt
```

확인:

```bash
find config/certs -maxdepth 1 \
  -type f -printf '%M %u:%g %f\n'
```

정상 예:

```text
-rw-r--r-- 1883:1883 fan-ca.crt
-rw------- 1883:1883 mqtt-server.key
-rw-r--r-- 1883:1883 mqtt-server.crt
```

---

## 5. MQTT 사용자와 ACL

### 5.1 ESP32 사용자 생성

최초 사용자 및 새 password file 생성:

```bash
docker exec -it mosquitto \
  mosquitto_passwd -c \
  /mosquitto/config/password_file \
  wifi_fan_device
```

`-c`는 파일을 새로 만들며 기존 파일을 덮어쓴다. 이후 사용자를 추가할 때는
절대로 `-c`를 사용하지 않는다.

```bash
docker exec -it mosquitto \
  mosquitto_passwd \
  /mosquitto/config/password_file \
  another_device
```

권한:

```bash
sudo chown 1883:1883 config/password_file
sudo chmod 600 config/password_file
```

### 5.2 `acl_file`

```conf
user wifi_fan_device

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

```bash
sudo chown 1883:1883 config/acl_file
sudo chmod 640 config/acl_file
```

새 프로젝트에서는 장치마다 별도 사용자를 만들고 해당 장치의 topic prefix만
허용한다. 모든 장치에 `topic readwrite #`를 주지 않는다.

---

## 6. Mosquitto TLS 동작 확인

### 6.1 컨테이너와 listener 확인

```bash
docker ps --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
```

필요한 포트:

```text
0.0.0.0:1883->1883/tcp
0.0.0.0:8883->8883/tcp
```

```bash
tail -n 100 /home/ej3989/docker/mosquitto/log/mosquitto.log
```

### 6.2 인증서와 호스트 이름 확인

```bash
cd /home/ej3989/docker/mosquitto

openssl s_client \
  -connect 127.0.0.1:8883 \
  -servername ej3989.iptime.org \
  -CAfile config/certs/fan-ca.crt \
  -verify_hostname ej3989.iptime.org \
  </dev/null
```

정상 결과:

```text
Verification: OK
Verified peername: ej3989.iptime.org
Verify return code: 0 (ok)
```

### 6.3 MQTT 로그인과 ACL 확인

비밀번호가 shell history에 남지 않도록 변수로 입력한다.

```bash
read -rsp "MQTT password: " MQTT_TEST_PW
echo
```

로컬에서는 접속 주소 `127.0.0.1`과 인증서 이름이 다르므로 이 테스트에서만
`--insecure`를 사용한다.

```bash
docker exec \
  -e MQTT_TEST_PW="$MQTT_TEST_PW" \
  mosquitto sh -c \
  'mosquitto_sub \
    -h 127.0.0.1 \
    -p 8883 \
    --cafile /mosquitto/config/certs/fan-ca.crt \
    --insecure \
    -u wifi_fan_device \
    -P "$MQTT_TEST_PW" \
    -t wifi_fan/power/set \
    -W 3 \
    -d'
```

성공 기준:

```text
received CONNACK (0)
received SUBACK
Subscribed (mid: 1): 0
```

```bash
unset MQTT_TEST_PW
```

`--insecure`는 로컬 주소 불일치 테스트에서만 사용한다. ESP32나 외부 실제
클라이언트에서는 인증서와 호스트 이름 검증을 절대로 끄지 않는다.

---

## 7. DDNS, 공인 IP, 포트포워딩

### 7.1 DDNS 확인

```bash
getent ahostsv4 ej3989.iptime.org
```

외부 공인 IP 확인:

```bash
curl -4 https://api.ipify.org
echo
```

DDNS가 반환한 IP, 공유기의 WAN IP, 외부 공인 IP가 일치해야 한다. 공유기 WAN
IP가 사설 주소이거나 외부 공인 IP와 다르면 CGNAT 또는 이중 NAT일 수 있다.

대표적인 사설/CGNAT 범위:

```text
10.0.0.0/8
100.64.0.0/10
172.16.0.0/12
192.168.0.0/16
```

CGNAT이면 일반 포트포워딩으로 외부에서 Raspberry Pi에 접속할 수 없다. 통신사
공인 IP, 외부 broker, VPS 또는 VPN 구조가 필요하다.

### 7.2 공유기 설정

```text
프로토콜: TCP
외부 포트: 8883
내부 IP: 192.168.0.66
내부 포트: 8883
```

다음 포트는 외부에 열지 않는다.

```text
1883  평문 MQTT
8123  Home Assistant 웹 UI
80    자체 CA에서는 불필요
```

외부 시험은 집 Wi-Fi가 아닌 휴대전화 핫스팟 등 실제 외부 네트워크에서 수행한다.
일부 공유기는 NAT loopback을 지원하지 않으므로 집 내부에서 DDNS로 연결이 안 돼도
외부에서는 정상일 수 있다.

외부 인증서 시험:

```bash
openssl s_client \
  -connect ej3989.iptime.org:8883 \
  -servername ej3989.iptime.org \
  -CAfile fan-ca.crt \
  -verify_hostname ej3989.iptime.org \
  </dev/null
```

외부 시험에서는 `--insecure` 또는 인증서 검증 비활성화를 사용하지 않는다.

---

## 8. Home Assistant 구성

### 8.1 Docker 상태

현재 Home Assistant는 다음 구성이다.

```text
container: homeassistant
image: ghcr.io/home-assistant/home-assistant:stable
network mode: host
config: /home/ej3989/docker/homeassistant/config -> /config
D-Bus: /run/dbus -> /run/dbus
```

Home Assistant는 MQTT integration을 통해 Raspberry Pi의 Mosquitto `1883`에
연결한다.

```text
설정 → 장치 및 서비스 → MQTT
Broker: 192.168.0.66
Port: 1883
```

현재 로컬 1883 listener는 익명 연결을 허용하므로 사용자명과 비밀번호가 없어도
동작한다. 향후 로컬 listener도 인증하도록 바꾸면 Home Assistant 전용 계정을
만들고 MQTT integration을 재구성한다.

### 8.2 MQTT Discovery

ESP32는 연결할 때 retained discovery payload를 발행한다. 기본 discovery prefix는
`homeassistant`이다.

```text
homeassistant/fan/wifi_fan_esp32s3/fan/config
homeassistant/sensor/wifi_fan_esp32s3/rssi/config
```

이 메시지를 받은 Home Assistant는 다음 장치를 자동 생성한다.

```text
Device: Bedroom Fan Controller
Entity: Bedroom Fan
Entity: Wi-Fi RSSI
```

Home Assistant에 장치가 보이지 않을 때 확인할 순서:

1. Home Assistant MQTT integration 연결 상태
2. ESP32 로그의 `Connected to MQTT broker`
3. ESP32 로그의 `MQTT command topics subscribed`
4. Mosquitto ACL에서 discovery topic write 허용 여부
5. `homeassistant/.../config` retained 메시지 존재 여부

### 8.3 MQTT topic

| 목적 | Topic | 방향 |
|---|---|---|
| 전원 명령 | `wifi_fan/power/set` | HA → ESP32 |
| 전원 상태 | `wifi_fan/power/state` | ESP32 → HA |
| 풍속 명령 | `wifi_fan/speed/set` | HA → ESP32 |
| 풍속 상태 | `wifi_fan/speed/state` | ESP32 → HA |
| 회전 명령 | `wifi_fan/oscillation/set` | HA → ESP32 |
| 회전 상태 | `wifi_fan/oscillation/state` | ESP32 → HA |
| 온라인 상태 | `wifi_fan/availability` | ESP32/Mosquitto → HA |
| Wi-Fi RSSI | `wifi_fan/wifi_rssi/state` | ESP32 → HA |

동작 규칙:

- command 메시지는 retain하지 않는다.
- ESP32는 안전을 위해 retained command를 무시한다.
- state, RSSI, availability, discovery는 retain한다.
- 비정상 연결 종료 시 Last Will이 `offline`을 발행한다.
- ESP32 부팅 시 릴레이는 모두 OFF로 초기화한다.

---

## 9. Zephyr `wifi_fan` TLS 구현

### 9.1 프로젝트 구조

```text
EJ_APP/wifi_fan/
├── CMakeLists.txt
├── Kconfig
├── prj.conf
├── wifi_fan_private.conf
├── wifi_fan_private.conf.example
├── certs/
│   └── fan-ca.crt
└── src/
    ├── main.c
    ├── fan_controller.c/.h
    ├── wifi_manager.c/.h
    ├── time_manager.c/.h
    ├── mqtt_tls.c/.h
    └── mqtt_fan.c/.h
```

### 9.2 CA 인증서를 프로젝트로 복사

Mac에서 실행:

```bash
mkdir -p /Volumes/ej_disk/zephyrproject/EJ_APP/wifi_fan/certs

scp \
  ej3989@192.168.0.66:/home/ej3989/docker/mosquitto/config/certs/fan-ca.crt \
  /Volumes/ej_disk/zephyrproject/EJ_APP/wifi_fan/certs/fan-ca.crt
```

복사 확인:

```bash
openssl x509 \
  -in /Volumes/ej_disk/zephyrproject/EJ_APP/wifi_fan/certs/fan-ca.crt \
  -noout -subject -issuer -dates -fingerprint -sha256
```

공개 `fan-ca.crt`만 프로젝트에 넣는다. 다음 파일은 복사하지 않는다.

```text
fan-ca.key
mqtt-server.key
password_file
```

### 9.3 CMake 인증서 내장

`CMakeLists.txt`는 `fan-ca.crt`를 바이트 배열 include 파일로 변환한다.

```cmake
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)

generate_inc_file_for_target(
  app
  ${APPLICATION_SOURCE_DIR}/certs/fan-ca.crt
  ${gen_dir}/fan-ca.crt.inc
)
```

`mqtt_tls.c`는 생성된 데이터를 펌웨어 읽기 전용 데이터에 포함한다.

```c
static const unsigned char fan_ca_certificate[] = {
#include <fan-ca.crt.inc>
    0x00
};
```

PEM 파서가 문자열 종료를 확인할 수 있도록 마지막 `0x00`이 필요하다.

부팅 시 Zephyr TLS credential manager에 등록한다.

```c
tls_credential_add(MQTT_CA_CERT_TAG,
                   TLS_CREDENTIAL_CA_CERTIFICATE,
                   fan_ca_certificate,
                   sizeof(fan_ca_certificate));
```

### 9.4 필요한 Kconfig

핵심 설정:

```conf
CONFIG_DNS_RESOLVER=y
CONFIG_SNTP=y

CONFIG_MQTT_LIB=y
CONFIG_MQTT_LIB_TLS=y
CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_NET_SOCKETS_TLS_CONNECT_TIMEOUT=15000
CONFIG_TLS_CREDENTIALS=y

CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=40000
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
CONFIG_MBEDTLS_PEM_PARSE_C=y
CONFIG_MBEDTLS_SSL_SERVER_NAME_INDICATION=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y
CONFIG_MBEDTLS_CIPHERSUITE_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256=y
CONFIG_PSA_CRYPTO=y
CONFIG_ENTROPY_GENERATOR=y

CONFIG_MAIN_STACK_SIZE=8192
```

Mosquitto의 `tls_version tlsv1.2`는 TLS 1.2 이상을 허용한다. ESP32 펌웨어는
현재 TLS 1.2 ECDHE-RSA AES-GCM ciphersuite를 사용하며 Mosquitto의 RSA 서버
인증서와 호환된다.

### 9.5 비공개 설정

`wifi_fan_private.conf`는 Git에 포함하지 않는다.

```conf
CONFIG_WIFI_FAN_WIFI_SSID="YOUR_WIFI_SSID"
CONFIG_WIFI_FAN_WIFI_PASSWORD="YOUR_WIFI_PASSWORD"

CONFIG_WIFI_FAN_MQTT_HOST="ej3989.iptime.org"
CONFIG_WIFI_FAN_MQTT_PORT=8883
CONFIG_WIFI_FAN_MQTT_USERNAME="wifi_fan_device"
CONFIG_WIFI_FAN_MQTT_PASSWORD="MOSQUITTO_DEVICE_PASSWORD"
```

`CONFIG_WIFI_FAN_MQTT_HOST`에는 숫자 공인 IP가 아닌 인증서 SAN과 동일한 도메인
이름을 사용한다. IP로 접속하면 TLS 호스트 이름 검증이 실패한다.

MQTT 비밀번호는 TLS로 전송 중 보호되지만 펌웨어 바이너리에는 포함된다. 물리적
펌웨어 추출까지 방어해야 하는 제품은 ESP32 Secure Boot, Flash Encryption,
보안 프로비저닝을 별도로 적용해야 한다.

### 9.6 부팅 및 연결 순서

```text
릴레이 모두 OFF
→ CA 인증서 등록
→ Wi-Fi 연결
→ DHCP IPv4 주소 획득
→ DNS 사용 가능
→ SNTP 시간 동기화
→ MQTT DDNS 조회
→ TCP 8883 연결
→ TLS CA/유효기간/호스트 이름/SNI 검증
→ MQTT 사용자 인증
→ command topic 구독
→ discovery/state/availability/RSSI 발행
```

SNTP가 필요한 이유는 인증서의 `notBefore`와 `notAfter`를 검사하기 위해서다.
ESP32가 부팅 직후 1970년으로 인식하면 올바른 인증서도 유효하지 않다고 거부한다.

### 9.7 MQTT TLS 설정 핵심

```c
mqtt_client_ctx.transport.type = MQTT_TRANSPORT_SECURE;

mqtt_client_ctx.transport.tls.config.peer_verify =
    TLS_PEER_VERIFY_REQUIRED;
mqtt_client_ctx.transport.tls.config.sec_tag_list = mqtt_tls_sec_tags;
mqtt_client_ctx.transport.tls.config.sec_tag_count = 1;
mqtt_client_ctx.transport.tls.config.hostname =
    CONFIG_WIFI_FAN_MQTT_HOST;
```

TLS를 사용할 때 poll 대상도 일반 TCP 소켓이 아닌 TLS 소켓이어야 한다.

```c
mqtt_poll_fd.fd = mqtt_client_ctx.transport.tls.sock;
```

### 9.8 빌드와 플래시

사용자가 직접 실행한다.

```bash
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

정상 로그:

```text
Wi-Fi fan controller start
All active-low relays initialized OFF (GPIO HIGH)
MQTT CA certificate registered
Connecting to Wi-Fi SSID '...'
Connected to Wi-Fi access point
IPv4 address acquired
Synchronizing time with 'pool.ntp.org'
System time synchronized (epoch ...)
Resolved MQTT host 'ej3989.iptime.org' to ...
Connecting to MQTT broker ej3989.iptime.org:8883
Connected to MQTT broker
MQTT command topics subscribed
Wi-Fi RSSI: -XX dBm
```

---

## 10. 선풍기 GPIO 동작

| 기능 | GPIO | 릴레이 |
|---|---:|---|
| 풍속 1 | GPIO4 | Active High, High 동안 ON |
| 풍속 2 | GPIO5 | Active High, High 동안 ON |
| 풍속 3 | GPIO6 | Active High, High 동안 ON |
| 회전 | GPIO7 | Active High, High 동안 ON |

- 기계식 버튼 구조이므로 해당 릴레이는 상태를 유지한다.
- 풍속 변경 전 세 풍속 릴레이를 모두 끄고 100ms 후 하나만 켠다.
- 전체 OFF 시 풍속과 회전 릴레이를 모두 끈다.
- 부팅 시 모든 릴레이를 OFF로 초기화한다.
- retained command는 재부팅 직후 선풍기가 자동으로 켜지는 것을 막기 위해 무시한다.

상용 전압과 모터를 제어할 때는 절연된 릴레이, 접점 정격, 퓨즈, 난연 케이스,
안전거리 및 전원 차단 절차가 필요하다.

---

## 11. 문제 진단표

| 증상/로그 | 의미 | 확인할 항목 |
|---|---|---|
| `MQTT host must be an IPv4 address` | 구버전 코드가 DDNS를 지원하지 않음 | `zsock_getaddrinfo()` 구현 사용 |
| `Failed to resolve MQTT host` | DNS 실패 | DHCP DNS, 공유기 DNS, DDNS 레코드 |
| `Failed to resolve SNTP host` | SNTP DNS 실패 | DNS와 인터넷 연결 |
| `SNTP query failed` | UDP 123 응답 없음 | 공유기/방화벽의 outbound UDP 123 |
| `MQTT TLS username and password must be configured` | 비공개 MQTT 자격증명 누락 | `wifi_fan_private.conf` |
| `certificate verify failed` | CA, 시간 또는 이름 불일치 | SNTP, CA 파일, SAN, SNI |
| `certificate is not yet valid` | ESP32 시간이 과거 | SNTP 성공 여부 |
| `certificate has expired` | 서버 또는 CA 인증서 만료 | 인증서 재발급 및 배포 |
| `err -116` | 연결 시간 초과 | DDNS, 공인 IP, CGNAT, 8883 포트포워딩 |
| MQTT `CONNACK` 거부 | 사용자 인증 실패 | 사용자명과 password_file |
| MQTT `SUBACK` 실패 | ACL 구독 거부 | command topic read 권한 |
| 상태 발행 후 연결 종료 | ACL publish 거부 가능 | state/discovery write 권한 |
| Home Assistant entity가 없음 | Discovery 미수신 | HA MQTT 연결, discovery ACL, retained config |
| Home Assistant에서 offline | ESP32 연결/LWT 상태 | ESP 로그와 Mosquitto 로그 |
| 로컬 `127.0.0.1` TLS 시험 실패 | 인증서는 DDNS 이름용 | `-verify_hostname` 또는 로컬 시험만 `--insecure` |

Mosquitto 로그:

```bash
tail -n 200 /home/ej3989/docker/mosquitto/log/mosquitto.log
```

컨테이너 설정과 마운트:

```bash
docker inspect mosquitto \
  --format '{{range .Mounts}}{{println .Source "->" .Destination}}{{end}}'
```

열린 포트:

```bash
sudo ss -ltnp | grep -E ':(1883|8883)\s'
```

---

## 12. 인증서 갱신과 백업

### 12.1 반드시 백업할 것

```text
ca-private/fan-ca.key
ca-private/fan-ca.srl
config/certs/fan-ca.crt
ca-private/mqtt-server.ext
Mosquitto CA 개인키 암호
```

CA 개인키가 유출되면 공격자가 신뢰되는 가짜 서버 인증서를 만들 수 있다. 유출이
의심되면 새 CA를 만들고 ESP32 펌웨어의 `fan-ca.crt`도 교체해야 한다.

CA 개인키를 잃어버리면 기존 서버 인증서가 만료될 때 같은 CA로 갱신할 수 없다.
새 CA 생성과 ESP32 펌웨어 업데이트가 필요하다.

### 12.2 서버 인증서 갱신

현재 서버 인증서 만료일은 `2028-11-04 UTC`다. 충분히 전에 갱신한다.

새 서버 키와 CSR을 `ca-private`에 임시로 만든 뒤 기존 `fan-ca.key`로 다시
서명한다. 첫 발급 이후에는 이미 `fan-ca.srl`이 있으므로 `-CAcreateserial`을
다시 사용하지 않는다.

```bash
cd /home/ej3989/docker/mosquitto

openssl genpkey \
  -algorithm RSA \
  -pkeyopt rsa_keygen_bits:2048 \
  -out ca-private/mqtt-server-new.key

openssl req \
  -new \
  -sha256 \
  -key ca-private/mqtt-server-new.key \
  -out ca-private/mqtt-server-new.csr \
  -subj "/C=KR/O=EJ Home/CN=ej3989.iptime.org"
```

```bash
openssl x509 \
  -req \
  -in ca-private/mqtt-server-new.csr \
  -CA config/certs/fan-ca.crt \
  -CAkey ca-private/fan-ca.key \
  -CAserial ca-private/fan-ca.srl \
  -out ca-private/mqtt-server-new.crt \
  -days 825 \
  -sha256 \
  -extfile ca-private/mqtt-server.ext
```

새 인증서를 배포하기 전에 반드시 검증한다.

```bash
openssl verify \
  -CAfile config/certs/fan-ca.crt \
  ca-private/mqtt-server-new.crt

openssl x509 \
  -in ca-private/mqtt-server-new.crt \
  -noout -subject -issuer -dates -ext subjectAltName
```

기존 인증서와 키를 한 세대 백업하고 새 파일을 올바른 권한으로 설치한다.

```bash
cp config/certs/mqtt-server.crt config/certs/mqtt-server.crt.previous
sudo cp config/certs/mqtt-server.key config/certs/mqtt-server.key.previous

sudo install -o 1883 -g 1883 -m 600 \
  ca-private/mqtt-server-new.key \
  config/certs/mqtt-server.key

sudo install -o 1883 -g 1883 -m 644 \
  ca-private/mqtt-server-new.crt \
  config/certs/mqtt-server.crt
```

Mosquitto가 새 인증서를 읽도록 재시작한 뒤 TLS 검증을 다시 수행한다.

```bash
cd /home/ej3989/docker/mosquitto
docker compose restart mosquitto

openssl s_client \
  -connect 127.0.0.1:8883 \
  -servername ej3989.iptime.org \
  -CAfile config/certs/fan-ca.crt \
  -verify_hostname ej3989.iptime.org \
  </dev/null
```

같은 `fan-ca.crt`로 서명했다면 ESP32 펌웨어를 다시 빌드할 필요가 없다. CA 자체를
바꾸거나 DDNS 이름을 바꾸면 ESP32 CA 또는 MQTT hostname 설정도 갱신해야 한다.

---

## 13. 새 MQTT TLS 프로젝트에 재사용하는 방법

새 장치를 만들 때 다음 순서로 적용한다.

### 서버에서

1. 장치 전용 MQTT 사용자 생성
2. 긴 고유 비밀번호 설정
3. 장치 전용 topic prefix 결정
4. `acl_file`에 최소 read/write 권한 추가
5. Mosquitto reload/restart
6. `fan-ca.crt` 공개 인증서를 개발 프로젝트로 복사

예:

```text
사용자: greenhouse_sensor
topic prefix: greenhouse/sensor1
```

```conf
user greenhouse_sensor
topic write greenhouse/sensor1/state
topic write greenhouse/sensor1/availability
topic read greenhouse/sensor1/set
topic write homeassistant/sensor/greenhouse_sensor/#
```

### 펌웨어에서

1. `certs/fan-ca.crt` 추가
2. CMake `generate_inc_file_for_target()` 추가
3. `mqtt_tls.c/.h` 재사용
4. `time_manager.c/.h` 재사용
5. TLS 관련 `prj.conf` 옵션 재사용
6. DDNS hostname과 8883 설정
7. 장치별 MQTT 사용자명과 비밀번호 설정
8. `MQTT_TRANSPORT_SECURE` 사용
9. TLS poll socket 사용
10. command/state/discovery topic을 새 장치 prefix로 변경

### Home Assistant에서

1. 동일한 Mosquitto broker를 계속 사용
2. 고유한 discovery topic 사용
3. `unique_id`와 `device.identifiers`를 장치마다 다르게 설정
4. state/command/availability topic이 ACL과 일치하는지 확인

장치별로 반드시 고유해야 하는 값:

```text
MQTT client ID
MQTT username/password
topic prefix
Home Assistant unique_id
Home Assistant device identifier
```

같은 client ID를 두 장치에서 사용하면 broker가 기존 연결을 끊을 수 있다.

---

## 14. 운영 전 최종 체크리스트

### Raspberry Pi

- [ ] Mosquitto 컨테이너가 실행 중이다.
- [ ] `1883`과 `8883` listener가 열린다.
- [ ] `mqtt-server.key` 권한은 `600`이다.
- [ ] `wifi_fan_device` password가 설정됐다.
- [ ] ACL은 필요한 topic만 허용한다.
- [ ] `openssl s_client` 검증 결과가 `0 (ok)`다.
- [ ] Mosquitto 데이터와 CA 개인키를 백업했다.

### 공유기/DDNS

- [ ] `ej3989.iptime.org`가 현재 공인 IP를 가리킨다.
- [ ] CGNAT이 아니다.
- [ ] TCP 8883만 `192.168.0.66:8883`으로 전달한다.
- [ ] 외부 TCP 1883, 8123, 80은 닫혀 있다.

### ESP32

- [ ] `fan-ca.crt`가 올바른 CA 인증서다.
- [ ] MQTT hostname은 `ej3989.iptime.org`다.
- [ ] MQTT port는 `8883`이다.
- [ ] 사용자명은 `wifi_fan_device`다.
- [ ] 비밀번호는 Mosquitto password와 동일하다.
- [ ] SNTP 동기화가 성공한다.
- [ ] CA/hostname/유효기간 검증을 끄지 않았다.
- [ ] retained command를 무시한다.

### Home Assistant

- [ ] MQTT integration이 로컬 Mosquitto에 연결됐다.
- [ ] `Bedroom Fan Controller`가 discovery로 생성됐다.
- [ ] 전원·풍속·회전 상태가 실제 릴레이 상태와 일치한다.
- [ ] ESP32 연결 해제 시 availability가 `offline`으로 바뀐다.
- [ ] RSSI가 약 30초 간격으로 갱신된다.

---

## 15. 핵심 보안 원칙

1. 인터넷에는 TLS 8883만 공개한다.
2. `allow_anonymous false`를 외부 listener에 적용한다.
3. 장치마다 서로 다른 사용자와 비밀번호를 사용한다.
4. ACL은 장치가 필요한 topic만 허용한다.
5. CA/서버 개인키와 비밀번호 파일을 Git에 넣지 않는다.
6. ESP32에서 CA, hostname, 유효기간 검증을 끄지 않는다.
7. `--insecure`는 로컬 진단 외에는 사용하지 않는다.
8. CA 개인키는 암호화해 오프라인 백업한다.
9. 인증서 만료일 전에 갱신 일정을 만든다.
10. 물리적 릴레이 장치는 네트워크 장애 시에도 수동으로 안전하게 끌 수 있어야 한다.

이 구성에서 TLS는 통신 암호화와 Mosquitto 서버 신원을 보장한다. 공유기
포트포워딩, 공인 IP, DDNS, MQTT 계정, ACL은 별개의 요소이며 모두 정상이어야
외부 ESP32 제어가 동작한다.
