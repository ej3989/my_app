# Zephyr AHT10 Driver Step-by-Step

이 문서는 `lvgl_practice`에서 사용하는 AHT10 드라이버를 기준으로 Zephyr의
외부 모듈 드라이버 구조를 단계별로 설명한다.

## 1. 최종 목표

전체 연결은 다음 순서로 이루어진다.

```text
overlay의 aht10 노드
        |
        v
aosong,aht10.yaml binding
        |
        v
DT_HAS_AOSONG_AHT10_ENABLED
        |
        v
CONFIG_WAVESHARE_35C_SENSOR_AHT10
        |
        v
aht10.c 컴파일
        |
        v
SENSOR_DEVICE_DT_INST_DEFINE()
        |
        v
struct device 객체 생성
        |
        v
sensor_sample_fetch() / sensor_channel_get()
```

## 2. 파일 구조

드라이버는 Zephyr 원본을 수정하지 않고 외부 모듈에 둔다.

```text
EJ_APP/modules/waveshare_35c_drivers/
├── zephyr/module.yml
├── Kconfig
├── CMakeLists.txt
├── dts/bindings/sensor/
│   └── aosong,aht10.yaml
└── drivers/sensor/
    ├── Kconfig
    ├── CMakeLists.txt
    └── aht10.c
```

`module.yml`의 `dts_root: .` 설정 때문에 모듈의 `dts/bindings`도 Zephyr가
binding 검색 경로로 사용한다.

## 3. Devicetree binding 만들기

파일:

```text
EJ_APP/modules/waveshare_35c_drivers/dts/bindings/sensor/aosong,aht10.yaml
```

내용:

```yaml
description: |
  Aosong AHT10 digital-output humidity and temperature sensor.

compatible: "aosong,aht10"

include: [sensor-device.yaml, i2c-device.yaml]
```

각 항목의 의미:

- `compatible`: overlay와 드라이버를 연결하는 식별 문자열이다.
- `sensor-device.yaml`: Zephyr sensor 장치 공통 속성을 상속한다.
- `i2c-device.yaml`: `reg`를 I2C 주소로 해석하게 한다.

## 4. Overlay에서 장치 선언하기

파일:

```text
EJ_APP/lvgl_practice/boards/esp32s3_devkitc_procpu.overlay
```

```dts
&i2c0 {
    status = "okay";

    aht10: aht10@38 {
        compatible = "aosong,aht10";
        reg = <0x38>;
        status = "okay";
    };
};
```

- `&i2c0`: 기존 ESP32-S3 I2C0 controller 노드를 수정한다.
- `aht10:`: `DT_NODELABEL(aht10)`에서 사용할 node label이다.
- `aht10@38`: node name과 unit address다.
- `reg = <0x38>`: AHT10의 7비트 I2C 주소다.

현재 보드 기본 pinctrl을 그대로 사용하므로 배선은 다음과 같다.

```text
AHT10 SDA -> ESP32-S3 GPIO1
AHT10 SCL -> ESP32-S3 GPIO2
AHT10 VCC -> 3.3 V
AHT10 GND -> GND
```

## 5. Kconfig로 드라이버 활성화하기

모듈 최상위 `Kconfig`가 sensor Kconfig를 읽는다.

```kconfig
rsource "drivers/sensor/Kconfig"
```

sensor Kconfig:

```kconfig
config WAVESHARE_35C_SENSOR_AHT10
    bool "Aosong AHT10 temperature and humidity sensor"
    default y
    depends on SENSOR
    depends on DT_HAS_AOSONG_AHT10_ENABLED
    select I2C
```

중요한 연결:

1. overlay의 compatible이 binding과 일치한다.
2. 노드가 `status = "okay"`이면
   `DT_HAS_AOSONG_AHT10_ENABLED=1`이 만들어진다.
3. `CONFIG_SENSOR=y` 조건도 만족하면 드라이버 설정이 활성화된다.
4. `select I2C`가 I2C subsystem을 활성화한다.

앱에서는 의존성을 명확하게 보이기 위해 다음을 직접 설정한다.

```conf
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_WAVESHARE_35C_SENSOR_AHT10=y
```

## 6. CMake로 소스 선택하기

최상위 drivers CMake에서 sensor 디렉터리로 들어간다.

```cmake
add_subdirectory(sensor)
```

sensor CMake는 Kconfig가 활성화된 경우에만 소스를 컴파일한다.

```cmake
zephyr_library()
zephyr_library_sources_ifdef(
    CONFIG_WAVESHARE_35C_SENSOR_AHT10
    aht10.c
)
```

## 7. 드라이버와 compatible 연결하기

`aht10.c` 맨 위의 정의가 핵심이다.

```c
#define DT_DRV_COMPAT aosong_aht10
```

Devicetree의 `aosong,aht10`에서 쉼표와 하이픈을 밑줄로 바꾼 C 식별자다.
이 정의 이후 `DT_INST_*` 매크로의 instance는 AHT10 노드를 의미한다.

## 8. config와 data 분리하기

```c
struct aht10_config {
    struct i2c_dt_spec bus;
};

struct aht10_data {
    uint32_t temperature_raw;
    uint32_t humidity_raw;
};
```

- `config`: 빌드할 때 정해지고 바뀌지 않는 I2C bus와 주소 정보다.
- `data`: 실행 중 측정할 때마다 바뀌는 raw 값이다.

`I2C_DT_SPEC_INST_GET(inst)`는 parent I2C controller와 `reg = <0x38>`을
묶어서 `struct i2c_dt_spec`을 만든다.

## 9. 초기화 순서

AHT10의 초기화는 다음 순서다.

```text
I2C controller 준비 확인
        |
        v
전원 안정화 20 ms 대기
        |
        v
E1 08 00 전송
        |
        v
10 ms 대기
        |
        v
상태 읽기 및 CAL bit 확인
```

관련 API:

```c
i2c_is_ready_dt(&config->bus);
i2c_write_dt(&config->bus, aht10_init_command,
             sizeof(aht10_init_command));
i2c_read_dt(&config->bus, &status, sizeof(status));
```

상태의 bit 3이 `1`이면 calibration이 활성화된 상태다.

## 10. 측정 요청과 데이터 읽기

측정 명령은 다음 3바이트다.

```c
static const uint8_t aht10_measure_command[] = {
    0xAC, 0x33, 0x00
};
```

측정 흐름:

```text
AC 33 00 전송
        |
        v
80 ms 대기
        |
        v
6바이트 읽기
        |
        v
busy bit 확인
        |
        v
20비트 습도와 온도 분리
```

6바이트 frame 구조:

```text
byte 0 : status
byte 1 : humidity[19:12]
byte 2 : humidity[11:4]
byte 3 : humidity[3:0] + temperature[19:16]
byte 4 : temperature[15:8]
byte 5 : temperature[7:0]
```

추출 코드:

```c
humidity_raw = sys_get_be24(&frame[1]) >> 4;
temperature_raw = sys_get_be24(&frame[3]) & 0x0FFFFF;
```

## 11. Raw 값을 실제 단위로 변환하기

습도 공식:

```text
RH = humidity_raw * 100 / 2^20
```

온도 공식:

```text
T = temperature_raw * 200 / 2^20 - 50
```

드라이버는 부동소수점 없이 micro 단위 정수로 계산한 다음
`struct sensor_value`의 `val1`, `val2`로 분리한다.

```c
value->val1 = micro_value / 1000000;
value->val2 = micro_value % 1000000;
```

## 12. Zephyr sensor API 연결하기

```c
static DEVICE_API(sensor, aht10_driver_api) = {
    .sample_fetch = aht10_sample_fetch,
    .channel_get = aht10_channel_get,
};
```

애플리케이션이 다음 함수를 호출하면 driver API 함수로 연결된다.

```c
sensor_sample_fetch(dev);
sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);
```

## 13. Device 객체 생성하기

```c
SENSOR_DEVICE_DT_INST_DEFINE(
    inst,
    aht10_init,
    NULL,
    &aht10_data_##inst,
    &aht10_config_##inst,
    POST_KERNEL,
    CONFIG_SENSOR_INIT_PRIORITY,
    &aht10_driver_api
);
```

이 매크로가 최종적으로 다음 정보를 가진 전역 device 객체를 만든다.

- 초기화 함수: `aht10_init`
- runtime data: `struct aht10_data`
- constant config: `struct aht10_config`
- API table: `aht10_driver_api`

## 14. 앱에서 장치 사용하기

`aht10_service.c`는 driver와 앱 controller 사이의 경계다.

```c
#define AHT10_NODE DT_NODELABEL(aht10)
static const struct device *const aht10_dev =
    DEVICE_DT_GET(AHT10_NODE);
```

`DEVICE_DT_GET()`은 새 객체를 동적 생성하지 않는다. 드라이버가 빌드 중 만든
device 전역 객체의 주소를 가져온다.

현재 앱은 `APP_EVENT_STATUS_TICK`마다 다음 흐름으로 읽는다.

```text
Zephyr timer, 5초
        |
        v
APP_EVENT_STATUS_TICK
        |
        v
app_controller thread
        |
        v
aht10_service_read()
        |
        v
sensor_sample_fetch()
        |
        v
sensor_channel_get()
```

측정에 성공하면 controller가 값을 `app_state`에 저장한다. LVGL thread는 센서를
직접 읽지 않고 1초마다 snapshot을 가져와 setup 화면의 label을 갱신한다.

```text
app_controller thread
        |
        v
app_state_set_aht10_reading()
        |
        v
mutex로 보호된 app_state
        |
        v
ui_state_timer_cb()
        |
        v
25.340°C / 48.120%RH
```

이 구조는 app controller와 LVGL thread가 동시에 AHT10 driver의 runtime data를
접근하는 것을 방지한다.

## 15. 생성 결과 확인하기

빌드 후 다음 파일들을 순서대로 확인한다.

### 최종 Devicetree

```sh
rg -n "aht10|aosong,aht10" EJ_APP/build/zephyr/zephyr.dts
```

### 생성된 Devicetree 매크로

```sh
rg -n "aht10|AOSONG_AHT10" \
  EJ_APP/build/zephyr/include/generated/zephyr/devicetree_generated.h
```

### 최종 Kconfig

```sh
rg -n "CONFIG_(SENSOR|I2C|WAVESHARE_35C_SENSOR_AHT10)=" \
  EJ_APP/build/zephyr/.config
```

### Device C symbol

```sh
rg -n "aht10|device_dts_ord" EJ_APP/build/zephyr/zephyr.map
```

## 16. 실행 시 예상 로그

정상적인 경우:

```text
<inf> aht10: AHT10 initialized, status=0x08
<inf> app_controller: AHT10 service ready
<inf> app_controller: AHT10: temperature=25340 mC humidity=48120 m%RH
```

값의 의미:

```text
25340 mC    = 25.340 degrees Celsius
48120 m%RH  = 48.120 %RH
```

## 17. 문제 해결 순서

### `DT_HAS_AOSONG_AHT10_ENABLED`가 없는 경우

- overlay의 `compatible` 철자 확인
- binding 파일 경로 확인
- `module.yml`의 `dts_root` 확인
- pristine build로 CMake/Devicetree 재생성

### `device_is_ready()`가 false인 경우

- SDA/SCL 배선 확인
- 3.3 V와 GND 확인
- I2C 주소 `0x38` 확인
- pull-up 저항 확인
- 드라이버 초기화 로그 확인

### `AHT10 read failed: -5`가 출력되는 경우

`-5`는 일반적으로 `-EIO`다. 다음 항목을 확인한다.

- SDA와 SCL이 뒤바뀌지 않았는지
- 센서가 AHT10이 맞는지
- 초기화 후 calibration bit가 설정되는지
- logic analyzer에서 `0x38` ACK가 보이는지

### 값이 고정되거나 이상한 경우

- 측정 명령 뒤 80 ms 대기가 유지되는지
- 6바이트 frame을 읽는지
- 20비트 마스크 `0x0FFFFF`가 적용되는지
- 너무 빠른 주기로 측정하지 않는지

## 18. 직접 해볼 다음 연습

1. AHT10 service의 로그 주기를 5초에서 2초로 변경한다.
2. 온도와 습도를 `app_state_snapshot`에 추가한다.
3. setup 화면의 온습도 값에 정상 범위별 색상을 적용한다.
4. `-EBUSY`일 때 한 번 더 기다리고 재시도하도록 수정한다.
5. I2C 오류 횟수를 state에 누적한다.
6. 센서 값이 기준을 넘으면 MAX98357A로 알림음을 출력한다.
