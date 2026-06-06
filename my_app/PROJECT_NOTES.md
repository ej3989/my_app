# my_app Notes

이 파일은 작업하면서 설명한 내용을 다시 볼 수 있게 정리하는 메모입니다.

## 기본 빌드/실행 명령

현재 보드는 ESP32-S3 DevKitC procpu이고, 16MB flash와 8MB PSRAM snippets를 같이 사용합니다.

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu my_app -S espressif-flash-16M -S espressif-psram-8M
west flash
west espressif monitor
```

Codex 내부 셸에서 `west`가 PATH에 없을 때는 `.venv/bin/west`를 사용하지만, 사용자는 위 명령처럼 venv 활성화 후 `west`로 실행하면 됩니다.

## CMakeLists.txt의 Zephyr 찾기

현재 `my_app/CMakeLists.txt`의 시작 부분은 다음과 같습니다.

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_app)

target_sources(app PRIVATE src/main.cpp)
```

이 중 가장 중요한 줄은 이것입니다.

```cmake
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

의미를 나누면 다음과 같습니다.

```text
find_package(Zephyr ...)
```

CMake에게 "Zephyr라는 패키지를 찾아서 이 프로젝트에 연결해라"라고 요청합니다.

Zephyr 패키지를 찾으면 Zephyr 빌드 시스템이 로드되고, 그때부터 `app`이라는 target, Kconfig, devicetree, board 설정, linker script 생성 같은 Zephyr 빌드 흐름이 준비됩니다.

```text
REQUIRED
```

반드시 찾아야 한다는 뜻입니다.

Zephyr를 못 찾으면 빌드를 계속하지 않고 에러로 멈춥니다.

```text
HINTS
```

CMake에게 "여기부터 먼저 찾아봐"라고 알려주는 검색 힌트입니다.

`HINTS`는 강제 경로가 아니라 우선 검색 후보입니다.

비슷하게 생각하면:

```text
Zephyr를 찾아야 하는데,
우선 이 경로를 먼저 확인해 봐.
```

입니다.

```text
$ENV{ZEPHYR_BASE}
```

CMake 변수 문법이 아니라 환경 변수 읽기 문법입니다.

즉 shell 환경 변수 `ZEPHYR_BASE` 값을 CMake 안에서 읽습니다.

예를 들어 shell에 이렇게 설정되어 있으면:

```sh
export ZEPHYR_BASE=/Volumes/ej_disk/zephyrproject/zephyr
```

CMake에서는 다음처럼 해석됩니다.

```cmake
find_package(Zephyr REQUIRED HINTS /Volumes/ej_disk/zephyrproject/zephyr)
```

현재 빌드 결과 기준으로 `build/CMakeCache.txt`에는 다음 값이 잡혀 있습니다.

```text
ZEPHYR_BASE:PATH=/Volumes/ej_disk/zephyrproject/zephyr
```

즉 현재 프로젝트의 Zephyr base는 이 경로입니다.

```text
/Volumes/ej_disk/zephyrproject/zephyr
```

주의할 점:

```cmake
$ENV{ZEPHYR_BASE}
```

는 환경 변수를 읽는 것이고,

```cmake
${ZEPHYR_BASE}
```

는 CMake 내부 변수를 읽는 것입니다.

`find_package(Zephyr ...)`가 성공하면 Zephyr 빌드 시스템 안에서 CMake 변수 `ZEPHYR_BASE`도 설정되어 이후 여러 Zephyr CMake 파일에서 `${ZEPHYR_BASE}` 형태로 사용됩니다.

### 왜 `project(my_app)`보다 먼저 있나?

일반 CMake 프로젝트에서는 보통 `project()`가 먼저 나옵니다.

하지만 Zephyr 앱에서는 `find_package(Zephyr ...)`를 먼저 호출하는 패턴을 사용합니다.

이유는 Zephyr가 먼저 빌드 환경을 준비해야 하기 때문입니다.

Zephyr가 준비한 뒤에:

```cmake
project(my_app)
```

로 현재 앱 프로젝트 이름을 정하고,

```cmake
target_sources(app PRIVATE src/main.cpp)
```

로 Zephyr가 만들어 둔 `app` target에 내 소스 파일을 붙입니다.

흐름은 이렇게 보면 됩니다.

```text
cmake_minimum_required()
-> find_package(Zephyr ...)
   -> Zephyr 빌드 시스템 로드
   -> board/Kconfig/devicetree/app target 준비
-> project(my_app)
-> target_sources(app PRIVATE src/main.cpp)
   -> 내 main.cpp를 Zephyr app target에 추가
```

## Zephyr drivers 폴더 구조

Zephyr에서 `drivers`와 관련된 폴더는 크게 두 종류로 보면 됩니다.

```text
zephyr/include/zephyr/drivers/
zephyr/drivers/
```

### `zephyr/include/zephyr/drivers`

이 폴더는 앱 코드가 include해서 사용하는 공통 API 헤더가 들어 있습니다.

예:

```cpp
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/led_strip.h>
```

여기 있는 파일들은 "앱이 어떤 함수와 타입을 쓸 수 있는지"를 정의합니다.

예를 들어 `gpio.h`에는 이런 것들이 있습니다.

```cpp
gpio_pin_configure_dt(...)
gpio_pin_set_dt(...)
gpio_pin_get_dt(...)
gpio_pin_interrupt_configure_dt(...)
gpio_add_callback(...)
struct gpio_dt_spec
struct gpio_callback
```

즉 `include/zephyr/drivers/gpio.h`는 GPIO 사용법의 공통 문법을 제공합니다.

하지만 이 파일 자체가 ESP32-S3 GPIO 레지스터를 직접 제어하는 구현은 아닙니다.

### `zephyr/drivers`

이 폴더는 실제 드라이버 구현과 빌드 선택 정보가 들어 있습니다.

예:

```text
zephyr/drivers/gpio/
zephyr/drivers/i2c/
zephyr/drivers/spi/
zephyr/drivers/i2s/
zephyr/drivers/led_strip/
zephyr/drivers/sensor/
zephyr/drivers/bluetooth/
zephyr/drivers/flash/
zephyr/drivers/pwm/
zephyr/drivers/adc/
```

각 드라이버 폴더에는 보통 다음 파일들이 있습니다.

```text
CMakeLists.txt
Kconfig
칩별/장치별 .c 파일
```

GPIO를 예로 들면:

```text
zephyr/drivers/gpio/
  CMakeLists.txt
  Kconfig
  gpio_esp32.c
  gpio_nrfx.c
  gpio_stm32.c
  gpio_mcux.c
  ...
```

`CMakeLists.txt`에는 어떤 config가 켜졌을 때 어떤 구현 파일을 빌드할지가 적혀 있습니다.

예:

```cmake
zephyr_library_sources_ifdef(CONFIG_GPIO_ESP32 gpio_esp32.c)
zephyr_library_sources_ifdef(CONFIG_GPIO_STM32 gpio_stm32.c)
zephyr_library_sources_ifdef(CONFIG_GPIO_NRFX gpio_nrfx.c)
```

의미는 다음과 같습니다.

```text
CONFIG_GPIO_ESP32가 켜져 있으면 gpio_esp32.c를 빌드에 넣어라.
CONFIG_GPIO_STM32가 켜져 있으면 gpio_stm32.c를 빌드에 넣어라.
CONFIG_GPIO_NRFX가 켜져 있으면 gpio_nrfx.c를 빌드에 넣어라.
```

앱 코드는 같은 `gpio_pin_set_dt()`를 호출하지만, 실제로는 현재 보드와 config에 맞는 구현 파일이 선택됩니다.

### 전체 흐름

현재 앱에서 GPIO를 쓰는 흐름은 이렇게 볼 수 있습니다.

```text
main.cpp
  #include <zephyr/drivers/gpio.h>
  gpio_pin_configure_dt(...)
  gpio_add_callback(...)

include/zephyr/drivers/gpio.h
  공통 GPIO API 선언

devicetree
  이 핀이 어떤 GPIO controller의 몇 번 핀인지 설명

Kconfig
  GPIO 사용 여부와 칩별 GPIO 드라이버 선택

drivers/gpio/gpio_esp32.c
  ESP32-S3 GPIO 실제 구현

ESP32-S3 hardware
  실제 레지스터 제어
```

즉 앱은 직접 ESP32-S3 레지스터를 만지지 않습니다.

Zephyr 드라이버 계층을 통과해서 하드웨어를 제어합니다.

### 왜 이렇게 나눠져 있나?

같은 앱 코드를 여러 보드에서 재사용하기 위해서입니다.

앱 코드는 이렇게 공통 API를 씁니다.

```cpp
gpio_pin_set_dt(&button_led, 1);
```

하지만 보드가 ESP32-S3이면 내부적으로 ESP32 GPIO 드라이버가 쓰이고, STM32면 STM32 GPIO 드라이버가 쓰입니다.

앱 코드는 최대한 그대로 두고:

```text
board
devicetree overlay
prj.conf
```

만 바꿔서 다른 하드웨어에 옮길 수 있게 하는 구조입니다.

### 정리

```text
zephyr/include/zephyr/drivers
  앱이 보는 공통 API

zephyr/drivers
  실제 구현, Kconfig, CMake 빌드 선택

devicetree
  어떤 하드웨어가 어디에 연결되어 있는지 설명

prj.conf / board conf
  어떤 기능과 드라이버를 켤지 설정
```

## Kconfig 파일의 언어 모드

Kconfig는 C도 YAML도 아니고 Kconfig 전용 문법입니다.

예:

```kconfig
config DISPLAY_NRF_LED_MATRIX
	bool "LED matrix driven by GPIOs"
	default y
	depends on DT_HAS_NORDIC_NRF_LED_MATRIX_ENABLED
	select NRFX_GPIOTE
	select NRFX_GPPI
	help
	  Enable driver for a LED matrix with rows and columns driven by
	  GPIOs.
```

이 문법은 최종적으로 `CONFIG_*` 값을 만들기 위한 설정 언어입니다.

```text
config DISPLAY_NRF_LED_MATRIX
  CONFIG_DISPLAY_NRF_LED_MATRIX 항목을 정의

bool
  true/false 설정

default y
  기본값 yes

depends on ...
  이 조건이 만족되어야 선택 가능

select ...
  이 config가 켜지면 다른 config도 같이 켬

help
  설정 설명
```

VS Code나 Cursor 기본 언어 모드 목록에 `Kconfig`가 없을 수 있습니다.

그 경우:

```text
files.associations에 "kconfig"를 넣어도
에디터가 kconfig 언어를 모르면 문법 강조가 안 됨
```

즉 파일 연결은 "이미 있는 언어 모드에 파일을 매칭"하는 설정이고, 없는 언어 모드를 새로 만들어 주지는 않습니다.

Kconfig 문법 강조를 쓰려면 Kconfig 확장을 설치해야 합니다.

확장을 설치한 뒤에는 `.vscode/settings.json`에 다음처럼 연결할 수 있습니다.

```json
{
  "files.associations": {
    "Kconfig": "kconfig",
    "Kconfig.*": "kconfig"
  }
}
```

확장을 설치하지 않은 상태라면 Kconfig 파일이 `Plain Text`처럼 보이는 것이 정상입니다.

## Zephyr 소스 파일을 열었을 때 include 오류가 보이는 이유

예를 들어 `zephyr/drivers/display/mb_font.c`에는 다음 include가 있습니다.

```c
#include <zephyr/display/mb_display.h>
```

이 헤더는 실제로 존재합니다.

```text
zephyr/include/zephyr/display/mb_display.h
```

따라서 이 줄 자체가 틀린 것은 아닙니다.

그런데 VS Code/Cursor에서 빨간 줄이 보일 수 있습니다.

이유는 현재 우리가 빌드하는 대상이 `my_app` + `esp32s3_devkitc`이기 때문입니다.

`mb_font.c`는 micro:bit display 쪽 드라이버 파일입니다.

현재 ESP32-S3 앱 빌드에는 보통 포함되지 않습니다.

Zephyr display CMake 파일에도 다음처럼 조건부로 들어갑니다.

```cmake
zephyr_library_sources_ifdef(CONFIG_MICROBIT_DISPLAY mb_display.c)
```

즉 `CONFIG_MICROBIT_DISPLAY`가 켜진 보드/설정에서만 관련 소스가 빌드에 들어갑니다.

현재 앱의 `compile_commands.json`은 `my_app`을 ESP32-S3 보드로 빌드한 결과이므로, micro:bit 전용 파일인 `mb_font.c`의 정확한 컴파일 옵션은 들어있지 않을 수 있습니다.

그래서 에디터가 이렇게 판단할 수 있습니다.

```text
이 파일은 현재 빌드 대상에 없음
-> compile_commands에서 이 파일의 include path를 못 찾음
-> include 오류 표시
```

하지만 실제 `my_app` 빌드에는 영향을 주지 않습니다.

정리:

```text
헤더가 실제로 없음
  진짜 코드/경로 문제

헤더는 있는데 에디터만 오류 표시
  현재 빌드 대상이 아닌 Zephyr 내부 파일을 열어서 생긴 IntelliSense 문제
```

현재 경우는 두 번째에 가깝습니다.

에디터 오류를 줄이고 싶으면 `.vscode/settings.json`에 `includePath`를 명시적으로 추가할 수 있습니다.

예:

```json
{
  "C_Cpp.default.includePath": [
    "${workspaceFolder}/my_app",
    "${workspaceFolder}/zephyr/include",
    "${workspaceFolder}/build/zephyr/include/generated",
    "${workspaceFolder}/modules/hal/espressif"
  ]
}
```

다만 Zephyr 내부의 모든 보드/드라이버 파일을 동시에 완벽하게 인식시키는 것은 어렵습니다.

Zephyr는 보드와 config에 따라 빌드되는 파일이 크게 달라지기 때문입니다.

## 현재 앱 기능

- WS2812 RGB LED 1개 제어
- GPIO 버튼 인터럽트 + debounce 처리
- 짧게 누르면 색 순환
- 길게 누르면 LED off
- shell 명령:
  - `rgb <red> <green> <blue>`
  - `mode <auto|manual>`
  - `status`
- BLE advertising 이름: `RGB Button`
- BLE 연결/해제 로그 출력
- BLE GATT characteristic으로 RGB 값 읽기/쓰기

## LED 상태를 RgbBlinker 안으로 모은 이유

처음에는 LED를 실제 하드웨어에 쓰는 `set_rgb_raw()` 함수가 class 밖에 있었습니다.

```cpp
static int set_rgb_raw(uint8_t red, uint8_t green, uint8_t blue)
{
	pixels[0] = {
		.r = red,
		.g = green,
		.b = blue,
	};

	int ret = led_strip_update_rgb(strip, pixels, ARRAY_SIZE(pixels));
	...
}
```

이 구조는 하드웨어에 직접 쓰는 부분을 분리해서 보기 쉽다는 장점이 있었지만, `pixels`, `current_color`, `strip` 같은 LED 상태가 class 밖 전역에 남는 단점이 있었습니다.

현재는 이 구조를 정리해서 `RgbBlinker`가 LED 관련 상태와 동작을 소유하게 했습니다.

```text
RgbBlinker
  strip_
  pixels_
  current_color_
  on_
  set()
  off()
  current_color()
  set_raw()
```

현재 구조:

```text
Zephyr led_strip driver
  ^
RgbBlinker::set_raw()
  ^
Button / Shell / BLE
```

`set_raw()`는 private 멤버 함수입니다.

```cpp
class RgbBlinker {
public:
	int set(uint8_t red, uint8_t green, uint8_t blue)
	{
		int ret = set_raw(red, green, blue);
		...
	}

private:
	int set_raw(uint8_t red, uint8_t green, uint8_t blue)
	{
		pixels_[0] = {
			.r = red,
			.g = green,
			.b = blue,
		};

		return led_strip_update_rgb(strip_, pixels_, ARRAY_SIZE(pixels_));
	}

	const device *strip_;
	led_rgb pixels_[kLedCount] = {};
	Color current_color_ = {};
	bool on_ = false;
};
```

BLE read는 더 이상 전역 `current_color`를 직접 읽지 않고, `RgbBlinker`의 getter를 통해 현재 색을 가져옵니다.

```cpp
const Color color = blinker_->current_color();
```

이렇게 바꾼 장점:

```text
1. LED 관련 상태가 RgbBlinker 안에 모임
2. 전역 변수 pixels/current_color가 사라짐
3. LED 제어 책임이 더 명확해짐
4. BLE, shell, button은 RgbBlinker의 public 함수만 사용하면 됨
```

정리하면, `RgbBlinker`는 이제 "RGB LED 하드웨어와 현재 LED 상태를 관리하는 객체"입니다.

## BLE 이름 설정 위치

BLE 이름은 `my_app/prj.conf`에서 설정합니다.

```conf
CONFIG_BT_DEVICE_NAME="RGB Button"
```

이 이름은 `main.cpp`의 scan response data에서 사용됩니다.

```cpp
BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1)
```

## BLE Write 테스트 방법

폰 BLE 앱에서 `RGB Button`에 연결한 뒤 custom characteristic에 write 합니다.

가장 안정적인 방식:

- Value type: `UTF-8`
- Write type: `Request`
- Value:

```text
20,0,0
```

색 예제:

```text
20,0,0     red
0,20,0     green
0,0,20     blue
20,20,20   white
0,0,0      off
```

성공하면 monitor에 이런 로그가 나옵니다.

```text
BLE RGB write: 20, 0, 0
```

Byte array 방식은 앱마다 입력 형식이 다를 수 있습니다. 현재 코드는 실제 3바이트가 들어오면 다음처럼 처리할 수 있습니다.

```text
14 00 00   red 20,0,0
00 14 00   green 0,20,0
00 00 14   blue 0,0,20
14 14 14   white 20,20,20
00 00 00   off
```

여기서 `14`는 16진수이며 10진수 `20`입니다.

## Write Request와 Write Command

`Write Request`는 클라이언트가 값을 쓴 뒤 서버 응답을 받는 방식입니다. 테스트할 때는 성공/실패 확인이 쉬워서 이 방식이 좋습니다.

`Write Command`는 응답 없이 보내는 방식입니다. 빠르지만 성공 여부 확인이 약합니다.

현재 characteristic은 둘 다 허용합니다.

```cpp
BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP
```

## BLE 코드를 클래스로 묶은 구조

Zephyr BLE API는 C API이기 때문에 callback 함수는 일반 함수 포인터 형태를 요구합니다. C++의 일반 멤버 함수는 `this` 포인터가 숨어 있어서 그대로 연결할 수 없습니다.

그래서 현재 코드는 `BluetoothRgbPeripheral` 클래스 안에 `static entry` 함수를 두고, 그 안에서 실제 객체 메서드로 넘기는 구조입니다.

```cpp
static BluetoothRgbPeripheral bluetooth(&blinker);
```

`main()`에서는 객체를 시작합니다.

```cpp
ret = bluetooth.start();
```

연결/해제 callback은 Zephyr 매크로에 static 함수를 연결합니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = BluetoothRgbPeripheral::connected_entry,
    .disconnected = BluetoothRgbPeripheral::disconnected_entry,
};
```

GATT characteristic도 static entry 함수를 연결합니다.

```cpp
BT_GATT_CHARACTERISTIC(&rgb_color_uuid.uuid,
                       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                       BluetoothRgbPeripheral::read_entry,
                       BluetoothRgbPeripheral::write_entry,
                       &bluetooth)
```

마지막의 `&bluetooth`가 중요합니다. 이 값이 `attr->user_data`로 들어갑니다. Zephyr가 read/write callback을 호출하면 static 함수 안에서 다시 객체를 꺼낼 수 있습니다.

```cpp
auto *self = static_cast<BluetoothRgbPeripheral *>(attr->user_data);
return self->write(conn, attr, buf, len, offset, flags);
```

전체 흐름은 다음과 같습니다.

```text
폰에서 BLE Write
 -> Zephyr C callback 호출
 -> BluetoothRgbPeripheral::write_entry()
 -> attr->user_data에서 bluetooth 객체 꺼냄
 -> bluetooth.write()
 -> blinker.set()
 -> LED 색 변경
```

## ButtonWatcher와 같은 패턴

`ButtonWatcher`도 비슷한 구조입니다.

Zephyr GPIO callback은 C 함수 포인터를 요구하므로 static 함수가 먼저 호출됩니다.

```cpp
gpio_init_callback(&callback_, ButtonWatcher::callback_entry, BIT(button_->pin));
```

그 다음 `CONTAINER_OF`로 callback 멤버를 가진 원래 객체를 찾습니다.

```cpp
auto *self = CONTAINER_OF(cb, ButtonWatcher, callback_);
self->schedule_debounce();
```

즉 Zephyr C API와 C++ 클래스를 연결할 때 자주 쓰는 패턴은 다음 둘 중 하나입니다.

```text
1. callback 구조체가 객체 멤버일 때: CONTAINER_OF 사용
2. attr->user_data처럼 사용자 포인터를 줄 수 있을 때: user_data에 객체 주소 저장
```

## my_app만 별도 Git 저장소로 관리하기

`zephyrproject` 전체를 Git으로 관리하지 않고 `my_app`만 독립 저장소로 관리하려면 `my_app` 폴더 안에서 Git을 초기화하면 됩니다.

현재 적용한 구조:

```text
zephyrproject/
  zephyr/        Zephyr SDK/source 쪽
  modules/       Zephyr modules
  build/         빌드 산출물
  my_app/        별도 Git 저장소
    .git/
    .gitignore
    CMakeLists.txt
    prj.conf
    boards/
    src/
    NOTES.md
```

초기화 명령:

```sh
cd /Volumes/ej_disk/zephyrproject/my_app
git init
git branch -m main
```

현재 추가 대상 파일 확인:

```sh
git status --short
```

처음 commit을 만들려면:

```sh
git add .
git commit -m "Initial my_app project"
```

`my_app/.gitignore`에는 Zephyr/CMake 빌드 산출물, 편집기 파일, 로그 파일이 들어가지 않도록 설정했습니다.

주의할 점:

- `west build ... my_app ...`는 계속 `zephyrproject` 루트에서 실행해도 됩니다.
- Git 명령은 `my_app` 폴더 안에서 실행해야 `my_app` 저장소에 적용됩니다.
- `build/`는 `zephyrproject/build`에 생기므로 `my_app` Git에는 들어가지 않습니다.
