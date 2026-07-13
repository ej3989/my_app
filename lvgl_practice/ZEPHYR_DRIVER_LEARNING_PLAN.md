# Zephyr Device Driver Practice Plan

이 문서는 `EJ_APP/lvgl_practice`에서 Zephyr 디바이스 드라이버를 단계적으로
학습하기 위한 기준 문서다. 앞으로 각 실습은 이 문서의 순서와 용어를 따른다.

빌드, 플래시, 보드 실행은 사용자가 직접 수행한다. 코드 설명과 검토에서는
항상 현재 소스, 생성 파일, 매크로 확장 결과를 함께 확인한다.

## 1. 최종 목표

다음 연결을 직접 구성하고 설명할 수 있는 상태가 목표다.

```text
Devicetree overlay
        |
        v
Devicetree binding YAML
        |
        v
generated Devicetree macros
        |
        +--------------------+
        |                    |
        v                    v
Kconfig dependency       Driver C macros
        |                    |
        v                    v
.config / autoconf.h     DEVICE_DT_INST_DEFINE()
        |                    |
        +---------+----------+
                  |
                  v
             struct device
                  |
                  v
        Application or subsystem API
```

학습 완료 후 다음 내용을 코드 기반으로 설명할 수 있어야 한다.

1. `prj.conf`, Kconfig, `.config`, `autoconf.h`의 차이
2. DTS, DTSI, overlay, binding YAML의 역할
3. `compatible` 문자열이 드라이버 C 파일까지 연결되는 과정
4. `module.yml`, CMake, Kconfig가 외부 모듈을 등록하는 과정
5. `config`, `data`, `api`, `struct device`의 역할
6. `DT_INST_FOREACH_STATUS_OKAY()`에서 장치 객체가 생성되는 과정
7. GPIO 인터럽트, work queue, SPI 전송, Input 이벤트의 호출 경로
8. XPT2046 드래그 처리와 좌표 보정이 기본 드라이버에서 어떻게 변경됐는지

## 2. 설명 및 실습 원칙

앞으로 각 단계는 동일한 형식으로 진행한다.

1. 이번 단계의 목표를 먼저 정한다.
2. 수정할 파일과 수정 이유를 설명한다.
3. 사용자가 코드를 직접 작성한다.
4. 작성한 코드를 소스 기준으로 검토한다.
5. 다음 생성 파일을 확인한다.

```text
EJ_APP/build/zephyr/.config
EJ_APP/build/zephyr/zephyr.dts
EJ_APP/build/zephyr/include/generated/zephyr/autoconf.h
EJ_APP/build/zephyr/include/generated/zephyr/devicetree_generated.h
```

6. 필요하면 `zephyr.map`에서 실제 C 심볼을 확인한다.
7. 사용자가 빌드 및 하드웨어 동작을 확인한 후 다음 단계로 이동한다.

단순히 최종 코드를 제공하는 방식보다 다음 질문에 답하는 방식으로 진행한다.

- 이 파일은 누가 읽는가?
- 이 매크로의 입력과 최종 확장 결과는 무엇인가?
- 이 값은 빌드 시간 값인가, 실행 시간 값인가?
- 이 객체는 Flash, RAM, stack 중 어디에 존재하는가?
- 이 함수는 어느 스레드 또는 ISR에서 호출되는가?

## 3. Zephyr 설정 파일 구분

### 3.1 `prj.conf`

애플리케이션이 요청하는 Kconfig 조각이다.

현재 예:

```conf
CONFIG_INPUT=y
CONFIG_INPUT_XPT2046=n
CONFIG_WAVESHARE_35C_INPUT_XPT2046=y
```

의미는 다음과 같다.

- Zephyr Input subsystem을 사용한다.
- Zephyr 기본 XPT2046 드라이버는 사용하지 않는다.
- 외부 모듈의 Waveshare 3.5C XPT2046 드라이버를 사용한다.

`prj.conf`는 최종 결과가 아니다. Kconfig가 읽는 입력 중 하나다.

### 3.2 모듈의 `Kconfig`

선택 가능한 설정 항목, 의존성, 자동 선택 관계를 정의한다.

현재 모듈의 입력 드라이버 설정:

```kconfig
config WAVESHARE_35C_INPUT_XPT2046
    bool "Waveshare 3.5C XPT2046 touch panel driver"
    default y
    depends on DT_HAS_XPTEK_XPT2046_WAVESHARE_35C_ENABLED
    select SPI
```

핵심은 `depends on`이다.

```text
overlay에 compatible 노드가 있고 status = "okay"
        |
        v
CONFIG_DT_HAS_XPTEK_XPT2046_WAVESHARE_35C_ENABLED=y
        |
        v
CONFIG_WAVESHARE_35C_INPUT_XPT2046 선택 가능
```

`select SPI`는 이 드라이버가 선택되면 SPI subsystem도 활성화하도록 요구한다.

### 3.3 `.config`

경로:

```text
EJ_APP/build/zephyr/.config
```

Kconfig가 모든 입력을 병합하고 의존성을 계산한 **최종 설정 결과**다.

현재 생성 결과에는 다음 값이 존재한다.

```conf
CONFIG_DT_HAS_XPTEK_XPT2046_WAVESHARE_35C_ENABLED=y
CONFIG_WAVESHARE_35C_INPUT_XPT2046=y
```

`.config`는 생성 파일이므로 직접 수정하지 않는다. 설정을 바꾸려면
`prj.conf`, 보드 `.conf`, Kconfig 또는 snippet을 수정한다.

### 3.4 `autoconf.h`

경로:

```text
EJ_APP/build/zephyr/include/generated/zephyr/autoconf.h
```

`.config` 결과를 C 전처리기가 사용할 수 있도록 바꾼 헤더다.

```c
#define CONFIG_WAVESHARE_35C_INPUT_XPT2046 1
```

CMake와 C 코드는 같은 설정을 서로 다른 형태로 사용한다.

```cmake
zephyr_library_sources_ifdef(
    CONFIG_WAVESHARE_35C_INPUT_XPT2046
    input_xpt2046.c
)
```

```c
#if defined(CONFIG_WAVESHARE_35C_INPUT_XPT2046)
/* C code */
#endif
```

## 4. Devicetree 파일 구분

### 4.1 SoC DTSI와 보드 DTS

- SoC DTSI: SoC가 가진 SPI, GPIO, I2S 같은 하드웨어 컨트롤러 정의
- 보드 DTS: 특정 보드에서 사용할 UART, Flash, chosen 등의 기본 연결
- 애플리케이션 overlay: 프로젝트에서 추가하거나 변경할 실제 장치 연결

overlay는 기존 트리를 처음부터 다시 만드는 파일이 아니다. 기존 노드를
참조하여 속성을 덮어쓰거나 자식 노드를 추가한다.

```dts
&spi2 {
    status = "okay";

    touch0: xpt2046@1 {
        compatible = "xptek,xpt2046-waveshare-35c";
        reg = <1>;
        status = "okay";
    };
};
```

- `&spi2`: 기존 SPI2 노드를 참조한다.
- `touch0:`: C 매크로에서 사용할 수 있는 node label이다.
- `xpt2046@1`: 노드 이름과 unit address다.
- `reg = <1>`: SPI chip-select 인덱스다.
- `compatible`: 이 노드의 형식과 드라이버를 연결하는 핵심 문자열이다.

### 4.2 Binding YAML

현재 커스텀 binding:

```text
EJ_APP/modules/waveshare_35c_drivers/
  dts/bindings/input/xptek,xpt2046-waveshare-35c.yaml
```

binding은 다음을 정의한다.

- 어떤 `compatible`을 담당하는가
- 어떤 속성이 필수인가
- 속성 타입은 무엇인가
- 기본값과 최소값은 무엇인가
- 어떤 공통 binding을 상속하는가

현재 binding은 `spi-device.yaml`을 포함하므로 `reg`,
`spi-max-frequency` 같은 SPI 장치 속성을 사용할 수 있다.

추가된 보정 속성:

```text
min-x, max-x, min-y, max-y
touchscreen-size-x, touchscreen-size-y
z-threshold, reads
swap-xy, invert-x, invert-y
```

YAML binding은 드라이버를 실행하지 않는다. DTS를 검증하고 C 매크로를
생성하는 규칙이다.

### 4.3 최종 `zephyr.dts`

경로:

```text
EJ_APP/build/zephyr/zephyr.dts
```

SoC DTSI, 보드 DTS, overlay를 모두 병합한 최종 Devicetree다.

overlay가 적용됐는지 확인할 때는 원본 overlay만 보지 말고 이 파일에서
최종 노드와 속성을 확인한다.

### 4.4 `devicetree_generated.h`

최종 Devicetree를 C 전처리기 토큰으로 변환한 파일이다.

예를 들어 다음 overlay 속성은:

```dts
touchscreen-size-x = <320>;
```

드라이버의 다음 코드에서 컴파일 시간 상수로 사용된다.

```c
DT_INST_PROP(index, touchscreen_size_x)
```

## 5. 현재 외부 모듈 구조

모듈 루트:

```text
EJ_APP/modules/waveshare_35c_drivers/
├── zephyr/module.yml
├── CMakeLists.txt
├── Kconfig
├── README.md
├── drivers/
│   ├── CMakeLists.txt
│   ├── display/
│   │   ├── CMakeLists.txt
│   │   ├── Kconfig
│   │   ├── display_ili9486.c
│   │   ├── display_ili9486.h
│   │   ├── display_ili9xxx.c
│   │   └── display_ili9xxx.h
│   └── input/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       └── input_xpt2046.c
└── dts/bindings/
    ├── display/ilitek,ili9486.yaml
    └── input/xptek,xpt2046-waveshare-35c.yaml
```

### 5.1 앱에서 모듈 등록

앱 `CMakeLists.txt`는 `find_package(Zephyr ...)`보다 먼저 모듈을 등록한다.

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_LIST_DIR}/../modules/waveshare_35c_drivers
)
```

Zephyr를 찾은 뒤 등록하면 module discovery가 이미 끝났기 때문에 늦다.

### 5.2 `zephyr/module.yml`

```yaml
name: waveshare_35c_drivers
build:
  cmake: .
  kconfig: Kconfig
  settings:
    dts_root: .
```

- `cmake`: 모듈 CMake 시작 위치
- `kconfig`: 모듈 Kconfig 시작 파일
- `dts_root`: `dts/bindings`를 찾을 기준 경로

### 5.3 CMake 연결

```text
module CMakeLists.txt
  -> drivers/CMakeLists.txt
     -> drivers/input/CMakeLists.txt
```

마지막 파일에서 Kconfig 결과에 따라 실제 C 파일을 컴파일한다.

```cmake
zephyr_library_sources_ifdef(
    CONFIG_WAVESHARE_35C_INPUT_XPT2046
    input_xpt2046.c
)
```

### 5.4 현재 활성 드라이버

현재 `lvgl_practice`는 다음 구성을 사용한다.

```text
LCD   : Zephyr 기본 CONFIG_ILI9488
Touch : 외부 모듈 CONFIG_WAVESHARE_35C_INPUT_XPT2046
```

모듈 안에는 커스텀 ILI9486 드라이버도 남아 있지만 현재 앱에서는 활성화하지
않았다. 파일이 모듈 폴더에 존재하는 것과 실제 빌드에 포함되는 것은 다르다.

## 6. 현재 XPT2046 드라이버 연결

### 6.1 `compatible` 연결

overlay:

```dts
compatible = "xptek,xpt2046-waveshare-35c";
```

binding:

```yaml
compatible: "xptek,xpt2046-waveshare-35c"
```

driver:

```c
#define DT_DRV_COMPAT xptek_xpt2046_waveshare_35c
```

문자열의 `,`와 `-`가 C 토큰에서는 `_`로 변환된다.

### 6.2 인스턴스 객체 생성

드라이버 마지막의 구조는 다음과 같다.

```c
#define XPT2046_DEFINE(index)                                  \
    static const struct xpt2046_config xpt2046_config_##index; \
    static struct xpt2046_data xpt2046_data_##index;           \
    DEVICE_DT_INST_DEFINE(index, ...)

DT_INST_FOREACH_STATUS_OKAY(XPT2046_DEFINE)
```

현재 compatible에 맞는 `status = "okay"` 노드가 하나이므로 개념적으로 다음과
같이 확장된다.

```c
XPT2046_DEFINE(0)
```

그 결과 장치별로 다음 객체가 만들어진다.

- 변경되지 않는 설정: `struct xpt2046_config`
- 실행 중 변경되는 상태: `struct xpt2046_data`
- Zephyr 공통 장치 객체: `struct device`

### 6.3 `config`와 `data`

`config`에는 빌드할 때 정해지는 값이 들어간다.

```text
SPI 장치 정보
인터럽트 GPIO 정보
화면 크기
raw 좌표 최소/최대값
평균 측정 횟수
축 교환 및 반전 설정
```

`data`에는 실행 중 바뀌는 값이 들어간다.

```text
device 포인터
GPIO callback 객체
work 및 delayable work
pressed 상태
마지막 좌표
SPI 수신 버퍼
```

## 7. 기본 XPT2046 대비 수정된 부분

비교 대상:

```text
기본: zephyr/drivers/input/input_xpt2046.c
수정: EJ_APP/modules/waveshare_35c_drivers/drivers/input/input_xpt2046.c
```

### 7.1 별도 compatible 사용

```c
/* 기본 */
#define DT_DRV_COMPAT xptek_xpt2046

/* 커스텀 */
#define DT_DRV_COMPAT xptek_xpt2046_waveshare_35c
```

기본 드라이버와 커스텀 드라이버가 같은 노드를 동시에 생성하지 않도록 분리했다.

### 7.2 좌표 방향 속성 추가

```c
bool swap_xy;
bool invert_x;
bool invert_y;
```

overlay와 binding에서 지정한 방향 보정을 드라이버 config에 저장한다.

### 7.3 좌표 범위 제한

커스텀 드라이버는 변환된 좌표에 `CLAMP()`를 적용한다.

```c
x = CLAMP(x, 0, config->screen_size_x - 1);
y = CLAMP(y, 0, config->screen_size_y - 1);
```

raw 값이 보정 범위를 조금 벗어나더라도 LVGL에 음수 또는 화면 밖 좌표를
전달하지 않게 한다.

### 7.4 측정과 보고를 공통 함수로 분리

커스텀 드라이버는 다음 함수를 추가했다.

```c
static bool xpt2046_sample_and_report(struct xpt2046_data *data);
```

이 함수가 다음 작업을 한 번에 수행한다.

```text
SPI 측정 반복
  -> 평균 계산
  -> swap/invert 적용
  -> 화면 좌표 변환
  -> pressure 판정
  -> INPUT_ABS_X/Y 보고
  -> INPUT_BTN_TOUCH press 보고
```

초기 press 처리와 드래그 중 반복 측정이 같은 함수를 사용한다.

### 7.5 드래그 polling 추가

```c
#define XPT2046_DRAG_POLL_MS 20
```

기본 드라이버는 press 이후 주로 release 여부를 확인한다. 커스텀 드라이버는
손가락이 눌린 동안 20 ms마다 다시 SPI 좌표를 읽고 Input 이벤트를 보낸다.

```text
GPIO IRQ
  -> xpt2046_isr_handler()
  -> k_work_submit(&data->work)
  -> xpt2046_work_handler()
  -> xpt2046_sample_and_report()
  -> k_work_reschedule(&data->dwork, 20 ms)
  -> xpt2046_release_handler()
  -> 아직 press이면 다시 sample/report 및 reschedule
```

이 변경 때문에 LVGL에서 단순 클릭뿐 아니라 연속적인 드래그 좌표를 받을 수 있다.

### 7.6 Active-low GPIO 해석

overlay:

```dts
int-gpios = <&gpio0 17 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
```

`gpio_pin_get_dt()`는 raw 전압이 아니라 Devicetree flag를 반영한 논리값을
반환한다.

```text
터치 press   -> 논리값 1
터치 release -> 논리값 0
```

따라서 release handler의 `== 0` 검사는 release 판정이다.

## 8. 전체 학습 순서

### Step 1. 현재 Touch 드라이버 선택 경로 추적

목표:

- overlay의 `compatible`에서 실제 C 파일까지 찾아간다.
- `.config`, `autoconf.h`, `zephyr.dts`, generated header를 직접 확인한다.
- 기본 XPT2046이 아니라 커스텀 XPT2046이 선택된 근거를 설명한다.

수정은 하지 않고 현재 빌드 결과를 읽는 단계다.

완료 조건:

```text
compatible
-> binding
-> DT_HAS_* Kconfig symbol
-> CONFIG_WAVESHARE_35C_INPUT_XPT2046
-> CMake source selection
-> DT_INST_FOREACH_STATUS_OKAY
-> struct device
```

경로를 말로 설명할 수 있다.

### Step 2. 가장 작은 가상 Counter 드라이버 작성

하드웨어 없이 전체 드라이버 생성 흐름을 먼저 연습한다.

예정 파일:

```text
modules/waveshare_35c_drivers/
├── drivers/misc/CMakeLists.txt
├── drivers/misc/Kconfig
├── drivers/misc/ej_counter.c
├── include/ej_counter.h
└── dts/bindings/misc/ej,counter.yaml
```

overlay 예제:

```dts
/ {
    counter0: counter {
        compatible = "ej,counter";
        step = <2>;
        status = "okay";
    };
};
```

배울 내용:

- 자체 driver API 구조체
- `DEVICE_DT_INST_DEFINE()`
- `dev->config`, `dev->data`, `dev->api`
- `DEVICE_DT_GET(DT_NODELABEL(counter0))`
- `device_is_ready()`

### Step 3. Counter 드라이버 생성 결과 추적

목표:

- `step` 속성이 generated macro가 되는 과정 확인
- `DT_INST_PROP(0, step)` 확장 확인
- `.config`에서 driver Kconfig 활성화 확인
- `zephyr.map`에서 `__device_dts_ord_*` 객체 확인
- 앱 API 호출이 `dev->api` 함수 포인터로 연결되는 과정 확인

### Step 4. GPIO Input 드라이버 작성

가상 장치 다음에는 GPIO interrupt를 사용하는 작은 Input 드라이버를 작성한다.

배울 내용:

- `GPIO_DT_SPEC_INST_GET()`
- `gpio_pin_configure_dt()`
- `gpio_init_callback()`과 `gpio_add_callback()`
- ISR에서 하면 안 되는 작업
- ISR에서 `k_work_submit()`으로 넘기는 이유
- `input_report_key()` 호출

### Step 5. XPT2046 드라이버 전체 해부

현재 커스텀 드라이버를 다음 순서로 읽는다.

```text
XPT2046_DEFINE
-> xpt2046_init
-> GPIO IRQ
-> work queue
-> SPI transaction
-> raw measurement
-> calibration
-> input_report_abs/key
-> Zephyr LVGL pointer input
```

각 함수의 실행 문맥도 함께 구분한다.

```text
초기화: system initialization context
ISR: GPIO interrupt context
work/dwork: system workqueue thread
LVGL 처리: LVGL thread
```

### Step 6. Touch 드라이버 개선 실습

현재 코드의 하드코딩과 구조를 개선한다.

예정 항목:

1. `XPT2046_DRAG_POLL_MS`를 binding 속성으로 이동
2. config에서 불필요한 const 제거 cast 정리
3. SPI 오류와 GPIO callback 재등록 오류 처리 강화
4. 좌표 변화가 없는 경우 불필요한 Input 이벤트 줄이기
5. 평균값과 간단한 노이즈 필터 비교
6. calibration 값 검증 추가

한 번에 모두 수정하지 않고 한 항목씩 적용하고 동작을 비교한다.

### Step 7. 드라이버 디버깅과 검증

배울 내용:

- `CONFIG_INPUT_LOG_LEVEL_DBG`
- 초기화 실패와 `device_is_ready()` 실패 추적
- binding 오류와 DTS 오류 구분
- Kconfig 미선택과 CMake 미컴파일 구분
- `undefined reference to __device_dts_ord_*` 분석
- GDB에서 `struct device`, `config`, `data` 확인
- stack과 system workqueue 사용량 확인

### Step 8. 모듈 정리와 재사용

마지막으로 모듈을 다른 앱에서도 사용할 수 있도록 정리한다.

- 모듈 README 갱신
- Kconfig help와 의존성 정리
- binding description과 단위 명시
- 공용 헤더 경로 정리
- 샘플 overlay 제공
- 기본 Zephyr 드라이버로 전환하는 방법 기록

## 9. 문제를 찾는 고정 순서

드라이버가 생성되지 않거나 동작하지 않을 때 다음 순서로 확인한다.

### 9.1 Devicetree

```text
overlay compatible이 정확한가?
status = "okay"인가?
binding이 발견됐는가?
최종 zephyr.dts에 노드가 존재하는가?
```

### 9.2 Kconfig

```text
DT_HAS_*_ENABLED가 y인가?
드라이버 CONFIG_*가 .config에서 y인가?
필요 subsystem이 선택됐는가?
```

### 9.3 CMake

```text
module이 find_package 전에 등록됐는가?
module.yml이 CMake/Kconfig/DTS root를 가리키는가?
zephyr_library_sources_ifdef 조건이 참인가?
```

### 9.4 C 드라이버

```text
DT_DRV_COMPAT이 compatible과 일치하는가?
DT_INST_FOREACH_STATUS_OKAY가 인스턴스를 생성하는가?
DEVICE_DT_INST_DEFINE의 init/config/data/api가 올바른가?
```

### 9.5 Runtime

```text
init 함수가 성공했는가?
device_is_ready()가 true인가?
GPIO/SPI 종속 장치가 준비됐는가?
ISR과 work handler가 호출되는가?
subsystem report 함수까지 도달하는가?
```

## 10. 진행 상태

- [ ] Step 1: 현재 Touch 드라이버 선택 경로 추적
- [ ] Step 2: 가상 Counter 드라이버 작성
- [ ] Step 3: 생성 결과와 device 객체 추적
- [ ] Step 4: GPIO Input 드라이버 작성
- [ ] Step 5: XPT2046 전체 호출 경로 해부
- [ ] Step 6: XPT2046 개선 항목 적용
- [ ] Step 7: 디버깅 및 오류 분석
- [ ] Step 8: 모듈 문서화 및 재사용 정리

다음 실습은 **Step 1: 현재 Touch 드라이버 선택 경로 추적**부터 시작한다.
