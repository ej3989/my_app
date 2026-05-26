## 다른 callback도 class로 쓸 수 있는가

대부분 가능합니다. 다만 callback API가 무엇을 매개변수로 넘겨주는지에 따라 객체 주소를 찾는 방식이 달라집니다.

Zephyr C API는 보통 C 함수 포인터를 요구합니다. 그래서 C++ 일반 멤버 함수는 바로 연결할 수 없고, 보통 아래 구조를 사용합니다.

```text
Zephyr C callback
 -> static entry 함수
 -> 객체 주소 찾기
 -> self->실제_멤버_함수()
```

객체 주소를 찾는 대표 방법은 3가지입니다.

## 1. callback 구조체를 객체 멤버로 넣고 CONTAINER_OF 사용

현재 `ButtonWatcher`가 이 방식입니다.

```cpp
class ButtonWatcher {
private:
	gpio_callback callback_ = {};

	static void callback_entry(const device *dev, gpio_callback *cb, uint32_t pins)
	{
		auto *self = CONTAINER_OF(cb, ButtonWatcher, callback_);
		self->schedule_debounce();
	}
};
```

왜 가능한가:

```text
Zephyr가 callback_entry(..., cb, ...) 호출
cb는 객체 안에 들어 있던 callback_ 멤버의 주소
CONTAINER_OF(cb, ButtonWatcher, callback_)로 바깥 객체 주소 계산
```

이 방식이 잘 맞는 callback:

```text
GPIO callback
k_work callback
k_timer callback
struct net_mgmt_event_callback 류
```

조건:

```text
callback 함수가 객체 안의 멤버 구조체 주소를 다시 넘겨줘야 함
```

## 2. user_data / user context 포인터를 제공하는 API

현재 BLE GATT read/write가 이 방식입니다.

```cpp
BT_GATT_CHARACTERISTIC(...,
		       BluetoothRgbPeripheral::read_entry,
		       BluetoothRgbPeripheral::write_entry,
		       &bluetooth)
```

마지막 `&bluetooth`가 `attr->user_data`에 저장됩니다.

```cpp
static ssize_t write_entry(..., const bt_gatt_attr *attr, ...)
{
	auto *self = static_cast<BluetoothRgbPeripheral *>(attr->user_data);
	return self->write(...);
}
```

왜 가능한가:

```text
등록할 때 객체 주소를 user_data에 저장
Zephyr가 callback 호출할 때 attr를 넘겨줌
attr->user_data에서 객체 주소를 다시 꺼냄
```

이 방식이 잘 맞는 callback:

```text
BLE GATT read/write callback
일부 driver callback 중 user_data/context 인자를 받는 API
```

조건:

```text
API가 void *user_data 또는 void *context를 저장하고 다시 넘겨줘야 함
```

## 3. 전역/static 객체 하나만 사용할 때 직접 접근

BLE connection callback은 현재 이 방식에 가깝습니다.

```cpp
static BluetoothRgbPeripheral bluetooth(&blinker);

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
};
```

현재 `connected_entry()`는 객체 상태를 쓰지 않기 때문에 그냥 static 함수로 충분합니다.

만약 객체 상태가 필요하다면 가장 단순하게는 전역 객체를 직접 참조할 수 있습니다.

```cpp
static void connected_entry(bt_conn *conn, uint8_t err)
{
	bluetooth.handle_connected(conn, err);
}
```

단점:

```text
객체가 하나라는 가정이 코드에 박힘
여러 BLE peripheral 객체를 만들기 어려움
```

그래도 embedded 앱에서는 하드웨어가 하나인 경우가 많아서 실용적으로 자주 씁니다.

## callback별 객체 찾기 가능 여부 예시

```text
GPIO interrupt callback
  가능
  방법: CONTAINER_OF(cb, MyClass, callback_member)

k_work callback
  가능
  방법: CONTAINER_OF(work, MyClass, work_member)

k_work_delayable callback
  가능
  방법:
    k_work_delayable_from_work(work)
    CONTAINER_OF(delayable, MyClass, delayable_member)

k_timer callback
  가능
  방법: CONTAINER_OF(timer, MyClass, timer_member)

BLE GATT read/write callback
  가능
  방법: attr->user_data에 객체 주소 저장

BLE connection callback
  제한적으로 가능
  이유: bt_conn_cb 자체는 section에 등록되는 static 구조체이고, 이벤트 때 객체 포인터를 직접 주지 않음
  방법:
    1. 객체 상태가 필요 없으면 static 함수만 사용
    2. 전역/static 객체 하나를 직접 호출
    3. conn별 상태가 필요하면 bt_conn에 맞는 별도 관리 테이블 사용
```

## 판단 기준

callback을 class로 묶고 싶으면 먼저 이것을 보면 됩니다.

```text
1. callback 인자에 내가 등록한 구조체 포인터가 다시 오는가?
   예: gpio_callback*, k_work*, k_timer*
   -> CONTAINER_OF 가능

2. 등록할 때 void *user_data/context를 넣을 수 있는가?
   예: bt_gatt_attr.user_data
   -> static_cast로 self 복구 가능

3. 둘 다 없는가?
   -> 전역/static 객체를 직접 참조하거나,
      별도 테이블로 conn/handle과 객체를 매핑해야 함
```

중요한 결론:

```text
entry 함수만 있다고 항상 객체 주소를 찾을 수 있는 것은 아님.
entry 함수가 받은 인자 중에 객체로 되돌아갈 단서가 있어야 함.
```

객체로 되돌아갈 단서는 보통 다음 둘 중 하나입니다.

```text
1. 객체 안에 들어 있던 멤버의 주소
2. 등록 시 저장해둔 user_data/context 포인터
```

## bt_conn *conn 매개변수 의미

BLE callback에 자주 나오는 `bt_conn *conn`은 "어떤 BLE 연결에서 이 이벤트나 요청이 왔는지"를 나타내는 연결 객체 포인터입니다.

예:

```cpp
static void connected_entry(bt_conn *conn, uint8_t err)
```

```cpp
static ssize_t write_entry(bt_conn *conn,
			   const bt_gatt_attr *attr,
			   const void *buf,
			   uint16_t len,
			   uint16_t offset,
			   uint8_t flags)
```

현재 앱은 동시에 여러 폰 연결을 관리하지 않고, 연결된 폰별로 상태를 따로 저장하지도 않습니다. 그래서 일부 callback에서는 `conn`을 실제로 쓰지 않습니다.

이럴 때는 Zephyr에서 제공하는 `ARG_UNUSED()`를 넣어 "일부러 안 쓰는 매개변수"라는 의도를 표시할 수 있습니다.

```cpp
static void connected_entry(bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err != 0) {
		printf("Bluetooth connection failed: 0x%02x\n", err);
		return;
	}

	printf("Bluetooth connected\n");
}
```

`conn`이 필요한 경우는 다음과 같습니다.

```text
1. 어떤 폰이 연결되었는지 구분할 때
2. 연결 객체를 저장해두고 나중에 notify/indicate를 보낼 때
3. 특정 연결을 강제로 끊을 때
4. 연결별 상태를 따로 관리할 때
5. connection parameter/security/MTU 같은 연결 정보를 다룰 때
```

예를 들어 나중에 버튼을 누를 때 폰으로 notification을 보내고 싶다면, 연결 시점에 `conn`을 저장해둘 수 있습니다.

```cpp
static bt_conn *current_conn;

static void connected_entry(bt_conn *conn, uint8_t err)
{
	if (err == 0) {
		current_conn = bt_conn_ref(conn);
	}
}

static void disconnected_entry(bt_conn *conn, uint8_t reason)
{
	if (current_conn != nullptr) {
		bt_conn_unref(current_conn);
		current_conn = nullptr;
	}
}
```

현재 단계에서는 read/write로 LED를 바꾸는 것이 목적이므로 `conn`을 쓰지 않아도 됩니다.

## BLE 연결 해제 후 다시 연결되게 하기

BLE peripheral은 advertising 중일 때 폰에서 발견하고 연결할 수 있습니다. 연결이 성립되면 connectable advertising은 멈출 수 있습니다.

따라서 연결이 끊어진 뒤 다시 연결하려면 advertising을 다시 시작해야 합니다.

현재 코드는 `BluetoothRgbPeripheral::start_advertising()`을 따로 만들고, 처음 시작할 때와 재연결 가능 시점에 같이 사용합니다.

```cpp
int start()
{
	int ret = bt_enable(nullptr);
	...

	return start_advertising();
}
```

처음 부팅 후:

```text
bt_enable()
 -> start_advertising()
 -> 폰에서 RGB Button 발견 가능
```

연결이 끊어진 후:

```cpp
static void recycled_entry()
{
	int ret = start_advertising();
	if (ret < 0 && ret != -EALREADY) {
		printf("Failed to restart Bluetooth advertising: %d\n", ret);
	}
}
```

`recycled` callback은 connection object가 stack 내부에서 반환된 뒤 호출됩니다. `disconnected` 직후보다 advertising을 다시 시작하기에 더 안전한 지점입니다.

callback 등록:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
	.recycled = BluetoothRgbPeripheral::recycled_entry,
};
```

흐름:

```text
폰 연결
 -> advertising 멈춤
 -> 폰 연결 해제
 -> disconnected_entry() 로그 출력
 -> Zephyr가 connection object 정리
 -> recycled_entry() 호출
 -> start_advertising()
 -> 다시 RGB Button으로 스캔/연결 가능
```

테스트할 때 monitor에서 기대할 수 있는 로그:

```text
Bluetooth advertising as "RGB Button"
Bluetooth connected
Bluetooth disconnected: 0x13
Bluetooth advertising as "RGB Button"
```

## main()이 return되어도 동작하는 이유

현재 `main()`은 초기화만 하고 return합니다.

```cpp
int main()
{
	int ret = blinker.init();
	...

	ret = bluetooth.start();
	...

	return 0;
}
```

`bluetooth.start()`가 우리 앱 전용 스레드를 직접 만드는 것은 아닙니다. 대신 Zephyr Bluetooth stack을 켜고 advertising을 시작합니다.

```cpp
int start()
{
	int ret = bt_enable(nullptr);
	...

	return start_advertising();
}
```

역할:

```text
bt_enable()
  Zephyr Bluetooth stack 초기화
  Bluetooth host/controller 관련 내부 처리 시작

bt_le_adv_start()
  BLE advertising 시작 요청
  이후 광고/연결 이벤트는 Bluetooth stack이 처리
```

Zephyr에서는 `main()`도 하나의 스레드입니다. `main()`이 return하면 main thread는 끝나지만, 커널 전체가 종료되는 것은 아닙니다.

계속 동작하는 이유:

```text
Zephyr kernel은 계속 실행 중
Bluetooth stack 내부 thread/workqueue가 동작
system workqueue가 동작
interrupt handler가 동작
shell backend thread가 동작
```

현재 앱의 비동기 동작들은 main loop가 아니라 callback 기반입니다.

```text
버튼 누름
 -> GPIO interrupt
 -> ButtonWatcher::callback_entry()
 -> k_work_delayable 예약
 -> system workqueue에서 debounce 처리

폰 BLE 연결
 -> Bluetooth stack
 -> connected_entry()

폰 BLE write
 -> Bluetooth stack
 -> GATT write_entry()
 -> LED 색 변경

폰 연결 해제
 -> Bluetooth stack
 -> disconnected_entry()
 -> recycled_entry()
 -> advertising 재시작

shell 명령 입력
 -> shell backend
 -> cmd_rgb(), cmd_mode(), cmd_status()
```

즉 현재 프로그램은 다음 구조입니다.

```text
main()
  초기화만 함
  callback 등록
  Bluetooth advertising 시작
  return

그 이후
  Zephyr kernel + interrupt + workqueue + Bluetooth stack + shell이 callback을 호출
```

일반 PC 프로그램처럼 `main()`이 끝나면 프로세스가 종료되는 모델과 다릅니다. Zephyr 같은 RTOS에서는 커널이 계속 살아 있고, 등록해둔 장치/스택/callback이 이벤트를 처리합니다.

그래도 명시적으로 main thread를 살려두고 싶다면 아래처럼 할 수도 있습니다.

```cpp
while (true) {
	k_sleep(K_FOREVER);
}
```

하지만 현재 앱에서는 필요하지 않습니다. 모든 동작이 callback 기반이기 때문입니다.
