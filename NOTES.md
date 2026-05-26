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
