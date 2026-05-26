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

## BLE 클래스화 전 코드 비교 파일

BLE 코드를 `BluetoothRgbPeripheral` 클래스로 묶기 전 형태는 아래 파일에 따로 남겼습니다.

```text
my_app/reference/bt_before_class.cpp
```

이 파일은 CMake 빌드 대상이 아닙니다. 즉, 보드에 올라가는 실제 코드는 여전히 `src/main.cpp`이고, `reference/bt_before_class.cpp`는 비교용입니다.

비교할 핵심 포인트:

```text
클래스화 전:
  전역 함수 read_rgb_characteristic()
  전역 함수 write_rgb_characteristic()
  전역 함수 start_bluetooth()
  BT_GATT_CHARACTERISTIC(..., read_rgb_characteristic, write_rgb_characteristic, nullptr)

클래스화 후:
  BluetoothRgbPeripheral::read_entry()
  BluetoothRgbPeripheral::write_entry()
  bluetooth.start()
  BT_GATT_CHARACTERISTIC(..., BluetoothRgbPeripheral::read_entry,
                              BluetoothRgbPeripheral::write_entry,
                              &bluetooth)
```

가장 중요한 차이는 마지막 인자입니다.

```cpp
// 클래스화 전
nullptr

// 클래스화 후
&bluetooth
```

클래스화 후에는 `&bluetooth`가 `attr->user_data`로 들어가고, static entry 함수에서 다시 객체를 꺼냅니다.

```cpp
auto *self = static_cast<BluetoothRgbPeripheral *>(attr->user_data);
return self->write(conn, attr, buf, len, offset, flags);
```

## BT_GATT_SERVICE_DEFINE가 하는 일

아래 코드는 BLE GATT 서비스를 Zephyr에 등록하는 부분입니다.

```cpp
BT_GATT_SERVICE_DEFINE(rgb_service,
	BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid),
	BT_GATT_CHARACTERISTIC(&rgb_color_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_rgb_characteristic,
			       write_rgb_characteristic,
			       nullptr),
);
```

역할을 나누면 다음과 같습니다.

```text
BT_GATT_SERVICE_DEFINE
  Zephyr에 GATT service를 정적으로 등록합니다.

rgb_service
  이 service 객체의 C 변수 이름입니다.

BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid)
  이 service의 UUID를 등록합니다.
  폰 앱에서 custom service로 보이는 큰 묶음입니다.

BT_GATT_CHARACTERISTIC(...)
  service 안에 characteristic을 하나 추가합니다.
  실제 read/write가 일어나는 항목입니다.
```

`BT_GATT_CHARACTERISTIC`의 인자는 다음 의미입니다.

```cpp
BT_GATT_CHARACTERISTIC(
    &rgb_color_uuid.uuid,             // characteristic UUID
    BT_GATT_CHRC_READ |
    BT_GATT_CHRC_WRITE |
    BT_GATT_CHRC_WRITE_WITHOUT_RESP,  // 클라이언트에 보여줄 기능
    BT_GATT_PERM_READ |
    BT_GATT_PERM_WRITE,               // 실제 접근 권한
    read_rgb_characteristic,          // read 요청이 왔을 때 호출
    write_rgb_characteristic,         // write 요청이 왔을 때 호출
    nullptr                           // callback에서 쓸 사용자 데이터
)
```

중요한 점은 이 코드가 직접 데이터를 처리하는 것이 아니라, "어떤 요청이 오면 어떤 함수를 호출할지"를 Zephyr에 알려준다는 점입니다.

흐름은 다음과 같습니다.

```text
폰에서 RGB Button 연결
 -> 폰 앱이 GATT service 목록을 검색
 -> Zephyr가 rgb_service와 rgb_color_uuid를 보여줌
 -> 폰에서 characteristic Read
 -> read_rgb_characteristic() 호출
 -> 폰에서 characteristic Write
 -> write_rgb_characteristic() 호출
```

즉, 선택한 코드는 "서비스를 연결하고 처리해주는 부분"이 맞지만, 더 정확히는 "서비스/characteristic을 등록하고, 처리 함수들을 Zephyr에 연결하는 선언부"입니다.

## BLE UUID 의미

아래 코드는 BLE service와 characteristic의 UUID를 정의합니다.

```cpp
static struct bt_uuid_128 rgb_service_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0000, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));

static struct bt_uuid_128 rgb_color_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x9f1d0001, 0x3d2f, 0x4f3a, 0x8b11, 0x123456789abc));
```

UUID는 BLE에서 service나 characteristic을 구분하기 위한 고유 ID입니다.

비유하면:

```text
BLE 기기 이름: RGB Button
  사람이 스캔 목록에서 보는 이름

Service UUID: 9f1d0000-3d2f-4f3a-8b11-123456789abc
  기능 묶음의 ID

Characteristic UUID: 9f1d0001-3d2f-4f3a-8b11-123456789abc
  실제 값을 읽고 쓰는 항목의 ID
```

폰 앱에서 보면 보통 이런 구조로 보입니다.

```text
RGB Button
  Service: 9f1d0000-3d2f-4f3a-8b11-123456789abc
    Characteristic: 9f1d0001-3d2f-4f3a-8b11-123456789abc
      Read
      Write
```

`rgb_service_uuid`는 큰 기능 묶음입니다. 지금 앱에서는 "RGB LED 제어 서비스"라고 생각하면 됩니다.

`rgb_color_uuid`는 그 서비스 안에 있는 실제 데이터 항목입니다. 지금 앱에서는 "RGB 색 값"입니다.

두 UUID가 거의 같고 `0000`, `0001`만 다르게 한 이유는 같은 기능 그룹임을 사람이 보기 쉽게 하기 위해서입니다.

```text
9f1d0000-...  RGB service
9f1d0001-...  RGB color characteristic
```

꼭 이렇게 해야 하는 것은 아니지만, custom BLE 서비스를 만들 때 흔히 쓰는 방식입니다.

Zephyr 매크로 구조는 다음과 같습니다.

```cpp
BT_UUID_128_ENCODE(...)
```

128-bit UUID 값을 Zephyr가 내부적으로 쓰는 바이트 순서로 인코딩합니다.

```cpp
BT_UUID_INIT_128(...)
```

인코딩된 값을 `bt_uuid_128` 구조체 초기값으로 만듭니다.

```cpp
static struct bt_uuid_128 rgb_service_uuid = ...
```

그 UUID 구조체를 전역/static 변수로 저장합니다. BLE 서비스 등록 시 이 주소를 넘깁니다.

## BT_CONN_CB_DEFINE 연결/해제 callback

아래 코드는 BLE 연결/해제 이벤트를 처리할 callback을 Zephyr에 등록하는 부분입니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = on_ble_connected,
	.disconnected = on_ble_disconnected,
};
```

즉, 이 부분도 callback 연결이 맞습니다.

역할은 다음과 같습니다.

```text
폰이 보드에 연결됨
 -> Zephyr BLE stack이 connected 이벤트 발생
 -> on_ble_connected() 호출

폰이 연결을 끊음
 -> Zephyr BLE stack이 disconnected 이벤트 발생
 -> on_ble_disconnected() 호출
```

클래스화 전에는 전역 함수를 직접 연결했습니다.

```cpp
static void on_ble_connected(bt_conn *conn, uint8_t err)
{
	printf("Bluetooth connected\n");
}

static void on_ble_disconnected(bt_conn *conn, uint8_t reason)
{
	printf("Bluetooth disconnected: 0x%02x\n", reason);
}
```

클래스화 후에는 static class 함수를 연결합니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
};
```

여기서도 중요한 점은 Zephyr가 C callback 함수 포인터를 요구한다는 것입니다. 그래서 일반 멤버 함수는 바로 연결할 수 없고, `static` 함수가 필요합니다.

GPIO callback과의 차이:

```text
GPIO callback:
  gpio_callback 구조체가 객체 멤버
  CONTAINER_OF로 원래 객체를 찾음

BLE connection callback:
  전역으로 등록되는 연결/해제 callback
  현재 코드에서는 객체 상태가 필요 없어서 static 함수만 연결

BLE GATT read/write callback:
  attr->user_data에 &bluetooth 저장
  callback 안에서 객체를 꺼내서 실제 메서드 호출
```

## connection_callbacks는 어디에 정의되는가

아래 코드에서 `connection_callbacks`는 Zephyr에 미리 정의된 이름이 아닙니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = on_ble_connected,
	.disconnected = on_ble_disconnected,
};
```

`connection_callbacks`는 우리가 매크로에 넘긴 이름입니다.

이 매크로는 `zephyr/include/zephyr/bluetooth/conn.h`에 정의되어 있습니다.

```cpp
#define BT_CONN_CB_DEFINE(_name) \
	static const STRUCT_SECTION_ITERABLE(bt_conn_cb, \
					     _CONCAT(bt_conn_cb_, _name))
```

따라서:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks)
```

는 개념적으로 다음과 비슷하게 확장됩니다.

```cpp
static const struct bt_conn_cb bt_conn_cb_connection_callbacks = {
	.connected = on_ble_connected,
	.disconnected = on_ble_disconnected,
};
```

실제로는 단순 전역 변수만 만드는 것이 아니라 `STRUCT_SECTION_ITERABLE` 때문에 Zephyr가 자동으로 찾을 수 있는 특별한 linker section에 들어갑니다.

즉 흐름은 다음과 같습니다.

```text
우리가 작성:
  BT_CONN_CB_DEFINE(connection_callbacks)

매크로가 생성:
  static const struct bt_conn_cb bt_conn_cb_connection_callbacks

Zephyr linker section에 배치:
  Bluetooth stack이 부팅/초기화 중 callback 목록에서 자동으로 발견

연결 이벤트 발생:
  .connected에 들어있는 함수 호출
```

`struct bt_conn_cb`는 같은 헤더에 정의되어 있으며, 내부에 여러 callback 함수 포인터가 있습니다.

```cpp
struct bt_conn_cb {
	void (*connected)(struct bt_conn *conn, uint8_t err);
	void (*disconnected)(struct bt_conn *conn, uint8_t reason);
	...
};
```

그래서 `connection_callbacks`는 "함수 이름"이 아니라, 연결 관련 함수 포인터들을 담는 `struct bt_conn_cb` 객체를 만들기 위한 이름입니다.

## GATT callback에서 self를 찾는 방식

현재 클래스화된 코드의 핵심은 아래 부분입니다.

```cpp
static ssize_t read_entry(bt_conn *conn,
			  const bt_gatt_attr *attr,
			  void *buf,
			  uint16_t len,
			  uint16_t offset)
{
	auto *self = static_cast<BluetoothRgbPeripheral *>(attr->user_data);

	return self->read(conn, attr, buf, len, offset);
}
```

여기서 `self`는 `attr->user_data`에서 꺼냅니다.

`bt_gatt_attr`는 Zephyr의 GATT attribute 구조체입니다. `zephyr/include/zephyr/bluetooth/gatt.h`에 정의되어 있고, 중요한 필드는 다음과 같습니다.

```cpp
struct bt_gatt_attr {
	const struct bt_uuid *uuid;
	bt_gatt_attr_read_func_t read;
	bt_gatt_attr_write_func_t write;
	void *user_data;
	uint16_t handle;
	uint16_t perm: 15;
};
```

즉 `bt_gatt_attr`는 다음 정보를 가진 "GATT 항목 하나"입니다.

```text
uuid        이 항목의 UUID
read        Read 요청 때 호출할 함수
write       Write 요청 때 호출할 함수
user_data   callback에서 다시 꺼내 쓸 사용자 데이터
handle      BLE stack이 관리하는 attribute 번호
perm        read/write 권한
```

GATT read callback 타입은 Zephyr에서 이렇게 정의되어 있습니다.

```cpp
typedef ssize_t (*bt_gatt_attr_read_func_t)(struct bt_conn *conn,
					    const struct bt_gatt_attr *attr,
					    void *buf,
					    uint16_t len,
					    uint16_t offset);
```

그래서 read callback에서는 `attr`가 항상 두 번째 매개변수입니다. 우리가 임의로 정한 순서가 아니라 Zephyr API가 정한 함수 시그니처입니다.

write callback도 두 번째 매개변수가 `attr`입니다.

```cpp
typedef ssize_t (*bt_gatt_attr_write_func_t)(struct bt_conn *conn,
					     const struct bt_gatt_attr *attr,
					     const void *buf,
					     uint16_t len,
					     uint16_t offset,
					     uint8_t flags);
```

`attr->user_data`에 `&bluetooth`가 들어가는 이유는 `BT_GATT_CHARACTERISTIC` 마지막 인자 때문입니다.

```cpp
BT_GATT_CHARACTERISTIC(&rgb_color_uuid.uuid,
		       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
		       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
		       BluetoothRgbPeripheral::read_entry,
		       BluetoothRgbPeripheral::write_entry,
		       &bluetooth)
```

마지막 인자 `&bluetooth`가 아래 매크로를 통해 `user_data` 필드에 들어갑니다.

```cpp
#define BT_GATT_ATTRIBUTE(_uuid, _perm, _read, _write, _user_data) \
{ \
	.uuid = _uuid, \
	.read = _read, \
	.write = _write, \
	.user_data = _user_data, \
	.handle = 0, \
	.perm = _perm, \
}
```

흐름은 다음과 같습니다.

```text
1. 우리가 static BluetoothRgbPeripheral bluetooth(&blinker); 생성

2. BT_GATT_CHARACTERISTIC 마지막 인자에 &bluetooth 전달

3. Zephyr 매크로가 bt_gatt_attr 객체 생성
   attr.user_data = &bluetooth
   attr.read = BluetoothRgbPeripheral::read_entry
   attr.write = BluetoothRgbPeripheral::write_entry

4. 폰에서 Read 요청

5. Zephyr가 해당 attr을 찾고 attr.read(conn, attr, ...) 호출

6. read_entry() 안에서 attr->user_data를 꺼냄

7. static_cast<BluetoothRgbPeripheral *>(attr->user_data)
   즉 void*였던 &bluetooth를 다시 BluetoothRgbPeripheral*로 해석

8. self->read(...) 호출
```

이 방식은 `bt_gatt_attr`가 특별히 C++ 객체를 아는 것이 아닙니다. 단지 `void *user_data`라는 빈 포인터 칸을 제공하고, 우리가 거기에 객체 주소를 넣었다가 다시 꺼내는 방식입니다.

비슷한 패턴:

```text
GPIO callback:
  cb 포인터만 받음
  CONTAINER_OF(cb, ButtonWatcher, callback_)로 객체 찾음

GATT callback:
  attr 포인터를 받음
  attr->user_data에 저장해둔 &bluetooth로 객체 찾음
```

