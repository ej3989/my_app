# LVGL Practice Project

ESP32-S3, Zephyr, LVGL, Waveshare 3.5-inch LCD를 이용해 UI부터 커스텀
드라이버, 센서, 오디오, 네트워크까지 단계적으로 학습하는 프로젝트다.

빌드와 보드 실행은 사용자가 직접 수행한다. 코드 변경 후에는 생성 파일과
로그를 함께 확인한다.

## 1. 현재 하드웨어

- ESP32-S3 DevKitC, 16 MB Flash, 8 MB PSRAM
- ILI9488 SPI LCD
- XPT2046 저항막 터치
- WS2812 LED
- AHT10 I2C 온습도 센서
- MAX98357A I2S Class-D 앰프
- 4옴 또는 8옴 스피커

MAX98357A 전원에는 저음 순간 전류 보충을 위해 1000 uF 벌크 커패시터를
추가했다. 데이터시트 권장 디커플링인 0.1 uF와 10 uF도 앰프 VIN/GND에
가깝게 배치하는 것이 좋다.

## 2. 완료된 기능

### UI와 입력

- LVGL 직접 초기화 및 전용 LVGL 스레드
- 메인 화면과 Setup 화면 전환
- Play/Stop, LED, MsgBox, Setup 버튼
- 스크롤 가능한 로그 컨테이너와 여러 줄 label
- XPT2046 터치 클릭 및 드래그
- 라디오 `PLAYING`/`STOPPED` 상태 표시
- AHT10 온도와 습도 표시

### 애플리케이션 구조

- `main.c`: 상태, 컨트롤러, UI 시작
- `app_controller.c`: 메시지 큐 기반 이벤트 처리
- `app_state.c`: mutex로 보호되는 공유 상태
- `app_settings.c`: Zephyr Settings/NVS 저장과 복원
- `led_service.c`: WS2812 LED 제어와 work queue
- `aht10_service.c`: Sensor API 래퍼
- `max98357a_service.c`: WAV와 PCM의 I2S 출력
- `wifi_service.c`: Wi-Fi 연결, DHCP, 전원 절약 설정
- `radio_service.c`: HTTP MP3 수신, minimp3 디코딩, I2S 스트리밍
- `lvgl_ej.c`: LVGL 객체 생성과 이벤트 전달

현재 호출 방향은 다음과 같다.

```text
LVGL callback
     |
     v
app_controller event queue
     |
     +--> app_state
     +--> led_service
     +--> max98357a_service
     +--> radio_service --> wifi_service
     +--> app_settings
```

UI callback에서 느린 하드웨어 작업을 직접 실행하지 않고 컨트롤러 메시지
큐에 요청을 전달하는 구조다.

### 인터넷 라디오

```text
HTTP MP3 stream
       |
       v
32 KB compressed MP3 buffer in PSRAM
       |
       v
minimp3 decoder
       |
       v
44.1 kHz / 16-bit / stereo PCM
       |
       v
I2S DMA --> MAX98357A --> speaker
```

완료된 안정화 작업:

- Wi-Fi power save 비활성화
- TCP RX packet/buffer와 수신 window 확대
- MP3 프리버퍼와 지속적인 nonblocking socket receive
- I2S prebuffer 및 underrun 복구
- Play/Stop 토글과 TCP socket shutdown
- 디지털 볼륨 감소
- MAX98357A 전원부 1000 uF 벌크 커패시터 추가

## 3. 외부 드라이버 모듈

경로:

```text
EJ_APP/modules/ej3989_drivers
```

구현된 드라이버:

- ILI9486 display
- Waveshare 보정 XPT2046 input
- AHT10 sensor

모듈 연결 흐름:

```text
module.yml
   |
   +--> dts_root: binding YAML 검색
   +--> cmake: driver source 등록
   +--> kconfig: CONFIG_EJ3989_* 등록
```

상세 학습 순서는 `ZEPHYR_DRIVER_LEARNING_PLAN.md`를 따른다.

## 4. 메모리 구성

PSRAM으로 이동한 항목:

- LVGL 64 KB memory pool
- LVGL 약 30 KB rendering buffer
- 32 KB MP3 compressed input buffer
- ESP Wi-Fi adapter heap

내부 DRAM에 유지하는 항목:

- I2S DMA memory slab
- PCM decode/output buffer
- 스레드 stack
- Zephyr network packet/buffer pool

I2S DMA 버퍼와 스레드 스택은 동작 안정성을 위해 무조건 PSRAM으로 옮기지
않는다. 메모리 사용은 `zephyr.map`과 빌드의 Memory region 출력으로 확인한다.

## 5. Wi-Fi 자격 증명

실제 SSID와 비밀번호는 Git에서 제외되는 다음 파일에 저장한다.

```text
lvgl_practice/wifi_credentials.conf
```

공개 저장소에는 예제 파일만 포함한다.

```text
lvgl_practice/wifi_credentials.conf.example
```

자세한 내용은 다음 문서를 참고한다.

```text
EJ_APP/WiFi_Credentials_Build.md
```

## 6. 최초 빌드 명령

`EJ_APP` 폴더에서 실행한다.

```sh
west build -p always -d build \
  -b esp32s3_devkitc/esp32s3/procpu \
  lvgl_practice \
  -S espressif-flash-16M \
  -S espressif-psram-8M \
  -- -DEXTRA_CONF_FILE=wifi_credentials.conf
```

같은 build 디렉터리의 이후 빌드:

```sh
west build -d build
```

Flash:

```sh
west flash -d build
```

## 7. 빌드 후 확인할 생성 파일

```text
build/zephyr/.config
build/zephyr/zephyr.dts
build/zephyr/include/generated/zephyr/autoconf.h
build/zephyr/include/generated/zephyr/devicetree_generated.h
build/zephyr/zephyr.map
```

확인 목적:

- `.config`: 최종 Kconfig 결과
- `zephyr.dts`: overlay가 병합된 최종 Devicetree
- `autoconf.h`: C에서 사용하는 `CONFIG_*` 매크로
- `devicetree_generated.h`: C에서 사용하는 `DT_*` 토큰
- `zephyr.map`: 객체와 버퍼의 실제 메모리 배치

## 8. 다음 실습 순서

### Step 1: 라디오 상태 모델 정리

현재 `bool radio_playing`을 다음 상태 enum으로 확장한다.

```c
enum app_radio_state {
	APP_RADIO_STOPPED,
	APP_RADIO_CONNECTING,
	APP_RADIO_BUFFERING,
	APP_RADIO_PLAYING,
	APP_RADIO_STOPPING,
	APP_RADIO_ERROR,
};
```

학습 목표:

- bool과 state machine 차이
- worker thread에서 UI thread로 상태 전달
- 오류 코드와 사용자 표시 분리
- 연결 중 Stop 처리

### Step 2: 런타임 볼륨 제어

LVGL slider를 추가하고 고정 `AUDIO_VOLUME_DIVISOR` 대신 정수 gain을 사용한다.

학습 목표:

- `LV_EVENT_VALUE_CHANGED`
- UI 값과 공유 상태 연결
- PCM saturation과 clipping
- 설정값을 NVS에 저장하고 재부팅 후 복원

### Step 3: 오디오 진단 모드

100 Hz와 1 kHz 사인파를 여러 진폭으로 출력한다.

학습 목표:

- sample rate와 PCM 생성
- 저음 스피커 왜곡과 전원 문제 구분
- I2S DMA block 단위 이해
- stereo를 mono로 안전하게 downmix

### Step 4: 네트워크 오류 복구

- Wi-Fi 연결 실패 재시도
- 스트림 socket 종료 후 재연결
- 수신 속도와 underrun count 상태 표시
- 사용자가 Stop하면 자동 재연결하지 않도록 구분

### Step 5: UI 구조 분리

현재 큰 `lvgl_ej_thread_handler()`를 다음 단위로 분리한다.

```text
ui_create_main_screen()
ui_create_setup_screen()
ui_update_state()
ui_set_radio_state()
```

학습 목표:

- LVGL 객체 수명과 screen ownership
- UI context 구조체
- 전역 `lv_obj_t *` 최소화
- callback user_data 관리

### Step 6: 커스텀 드라이버 심화

다음 순서로 기존 드라이버를 다시 분석한다.

1. AHT10: 가장 단순한 I2C sensor driver
2. XPT2046: SPI, GPIO interrupt, work queue, Input API
3. ILI9486: MIPI DBI와 Display API

각 드라이버에서 다음 연결을 직접 확인한다.

```text
overlay
  --> binding YAML
  --> devicetree_generated.h
  --> DT_INST_* macros
  --> DEVICE_DT_INST_DEFINE
  --> struct device
  --> subsystem API
```

### Step 7: 제품 수준 기능

기본 구조가 안정화된 후 진행한다.

- Wi-Fi 런타임 provisioning
- HTTPS/TLS 인증서 검증
- 여러 라디오 station 관리
- watchdog과 fault recovery
- PM과 화면 절전
- firmware update

## 9. 현재 권장 다음 단계

바로 진행할 단계는 **Step 1: 라디오 상태 모델 정리**다.

현재 bool 상태는 연결 중, 버퍼링 중, 실제 재생 중, 오류 상태를 구분하지
못한다. 이 상태 모델을 먼저 정리하면 이후 볼륨 slider, 재연결, UI 오류 표시를
안전하게 추가할 수 있다.
