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
CONFIG_EJ3989_INPUT_XPT2046=y
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
config EJ3989_INPUT_XPT2046
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
CONFIG_EJ3989_INPUT_XPT2046 선택 가능
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
CONFIG_EJ3989_INPUT_XPT2046=y
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
#define CONFIG_EJ3989_INPUT_XPT2046 1
```

CMake와 C 코드는 같은 설정을 서로 다른 형태로 사용한다.

```cmake
zephyr_library_sources_ifdef(
    CONFIG_EJ3989_INPUT_XPT2046
    input_xpt2046.c
)
```

```c
#if defined(CONFIG_EJ3989_INPUT_XPT2046)
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
EJ_APP/modules/ej3989_drivers/
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
EJ_APP/modules/ej3989_drivers/
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
    ${CMAKE_CURRENT_LIST_DIR}/../modules/ej3989_drivers
)
```

Zephyr를 찾은 뒤 등록하면 module discovery가 이미 끝났기 때문에 늦다.

### 5.2 `zephyr/module.yml`

```yaml
name: ej3989_drivers
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
    CONFIG_EJ3989_INPUT_XPT2046
    input_xpt2046.c
)
```

### 5.4 현재 활성 드라이버

현재 `lvgl_practice`는 다음 구성을 사용한다.

```text
LCD   : Zephyr 기본 CONFIG_ILI9488
Touch : 외부 모듈 CONFIG_EJ3989_INPUT_XPT2046
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
수정: EJ_APP/modules/ej3989_drivers/drivers/input/input_xpt2046.c
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
-> CONFIG_EJ3989_INPUT_XPT2046
-> CMake source selection
-> DT_INST_FOREACH_STATUS_OKAY
-> struct device
```

경로를 말로 설명할 수 있다.

### Step 2. 가장 작은 가상 Counter 드라이버 작성

하드웨어 없이 전체 드라이버 생성 흐름을 먼저 연습한다.

예정 파일:

```text
modules/ej3989_drivers/
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

## 11. 추가 하드웨어 연결 계획

현재 보드에 다음 장치를 추가할 예정이다.

```text
I2S amplifier : MAX98357A
I2C sensor    : TS1012L-AHT10 온습도 모듈
```

### 11.1 현재 핀 점유 상태

```text
GPIO4, 5, 6, 15 : 버튼
GPIO8, 9         : LCD reset, D/C
GPIO10~13        : LCD/Touch SPI2
GPIO16, 17       : Touch CS, IRQ
GPIO38           : I2S0 기반 WS2812
GPIO43, 44       : UART0 console
GPIO19, 20       : USB Serial/JTAG
GPIO33~37        : N16R8 Octal PSRAM 관련 핀이므로 사용하지 않음
```

I2S0은 WS2812 드라이버가 LED waveform 생성용으로 사용한다. MAX98357A 오디오와
같은 I2S controller를 공유하면 두 드라이버가 서로 다른 sample format과 clock을
설정하게 되므로 I2S1을 오디오 전용으로 사용한다.

보드 기본 I2S1 pinctrl은 GPIO4, 5, 6을 사용하지만 이 프로젝트에서는 버튼과
충돌한다. 따라서 I2S1 pinctrl을 새로 정의한다.

### 11.2 권장 핀 배치

```text
ESP32-S3                 MAX98357A
-----------------------------------------
GPIO39  I2S1 BCLK   ->   BCLK
GPIO40  I2S1 WS     ->   LRC / LRCLK / WS
GPIO41  I2S1 DOUT   ->   DIN
GPIO42  GPIO output ->   SD / EN (선택 사항)
GND                  ->   GND
5V                   ->   VIN (전원 조건 확인)

ESP32-S3                 AHT10 module
-----------------------------------------
GPIO1   I2C0 SDA    <->  SDA
GPIO2   I2C0 SCL    ->   SCL
3V3                  ->  VCC
GND                  ->  GND
```

GPIO39~42는 ESP32-S3의 외부 JTAG signal 이름도 가진다. 이 배치는 GPIO39~42를
사용하는 외부 4-wire JTAG probe와 동시에 사용할 수 없다. 현재 내장 USB
Serial/JTAG를 GPIO19/20으로 사용하는 구성에서는 GPIO39~42를 I2S/GPIO로 사용할
수 있다.

### 11.3 MAX98357A 전원 주의점

MAX98357A는 MCLK가 필요하지 않고 BCLK, LRCLK, DIN 세 신호만으로 오디오를
수신한다. 그러나 speaker 출력 전력은 ESP32 GPIO 전원이 아니라 amplifier VIN
전원에서 공급된다.

- 작은 음량 시험은 보드 5V pin으로 시작할 수 있다.
- 큰 음량 또는 4 ohm speaker는 별도 5V 전원을 고려한다.
- 별도 전원을 쓰면 ESP32와 amplifier의 GND를 반드시 공통으로 연결한다.
- Class-D speaker 출력의 `SPK+`, `SPK-` 중 하나를 GND에 연결하지 않는다.
- breakout의 SD/MODE, GAIN pin 기본 회로를 확인한 뒤 GPIO42 사용 여부를 정한다.

### 11.4 I2S1 overlay 예정 구조

```dts
&pinctrl {
    i2s1_max98357: i2s1_max98357 {
        group1 {
            pinmux = <I2S1_O_BCK_GPIO39>,
                     <I2S1_O_WS_GPIO40>,
                     <I2S1_O_SD_GPIO41>;
        };
    };
};

&i2s1 {
    status = "okay";
    pinctrl-0 = <&i2s1_max98357>;
    pinctrl-names = "default";
};
```

ESP32-S3 SoC DTS에서 I2S1 DMA는 기본적으로 RX channel 4, TX channel 5에 연결된다.
I2S0 WS2812는 현재 TX channel 3을 사용하므로 TX DMA channel이 겹치지 않는다.

MAX98357A는 별도의 I2C 설정이 없는 단순 I2S sink다. 현재 Zephyr tree에는 이
부품 전용 codec binding/driver가 없으므로 애플리케이션의 audio service가
`i2s1` controller API를 직접 사용한다.

초기 오디오 설정 예정값:

```text
Format      : standard I2S
Sample rate : 16 kHz 또는 48 kHz
Word size   : 16 bit
Channels    : 2
Clock role  : ESP32-S3가 BCLK/WS controller
Direction   : TX
```

MAX98357A는 mono amplifier지만 I2S frame은 stereo 형태로 보내고, breakout의
SD/MODE 설정으로 left, right 또는 mono mix channel을 선택한다.

### 11.5 AHT10 overlay 예정 구조

보드 DTS가 이미 I2C0 pinctrl을 GPIO1(SDA), GPIO2(SCL)로 정의한다. overlay에서는
controller를 활성화하고 sensor child node를 추가한다.

```dts
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;

    aht10: aht10@38 {
        compatible = "aosong,aht10";
        reg = <0x38>;
        status = "okay";
    };
};
```

현재 Zephyr checkout에는 `aosong,aht20` driver는 있지만 `aosong,aht10` binding과
driver는 없다. AHT20 compatible을 임의로 사용하지 않고 다음 파일을 외부 모듈에
추가하는 실습으로 진행한다.

```text
EJ_APP/modules/ej3989_drivers/
├── drivers/sensor/CMakeLists.txt
├── drivers/sensor/Kconfig
├── drivers/sensor/aht10.c
└── dts/bindings/sensor/aosong,aht10.yaml
```

예정 driver API:

```text
sensor_sample_fetch()
sensor_channel_get(SENSOR_CHAN_AMBIENT_TEMP)
sensor_channel_get(SENSOR_CHAN_HUMIDITY)
```

AHT10의 I2C address는 고정 `0x38`을 사용한다. 처음에는 100 kHz standard mode와
3.3V 전원을 사용한다. TS1012L breakout에 regulator와 I2C pull-up이 포함됐는지는
실물 회로 또는 판매 문서를 확인해야 한다. 확인 전에는 5V logic pull-up을
가정하지 않는다.

### 11.6 추가 장치 실습 순서

```text
1. 기존 GPIO 점유표와 실제 보드 header 대조
2. I2C0만 활성화하고 bus/device address 확인
3. AHT10 binding 작성
4. AHT10 Kconfig/CMake 연결
5. AHT10 sensor driver 작성 및 LVGL 표시
6. I2S1 pinctrl과 controller 활성화
7. 정현파 PCM block을 만들어 MAX98357A 출력 확인
8. audio_service.c로 I2S buffer/work 구조 분리
9. AHT10 경고 조건과 audio 알림 통합

AHT10 구현의 파일별 코드와 실습 순서는
[`AHT10_DRIVER_TUTORIAL.md`](AHT10_DRIVER_TUTORIAL.md)를 참고한다.
```

I2C sensor driver가 Devicetree, binding, Kconfig, CMake, `struct device`, subsystem
API를 모두 포함하므로 가상 Counter 다음의 첫 실제 하드웨어 드라이버로 사용한다.
