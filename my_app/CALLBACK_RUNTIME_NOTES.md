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

## 버튼 long press로 60초 동안 BLE advertising 열기

BLE peripheral은 advertising 중일 때 폰에서 발견하고 연결할 수 있습니다. 현재 앱은 주변 사람이 항상 스캔/연결하지 못하도록 부팅 직후에는 advertising을 바로 켜지 않습니다.

대신 버튼을 long press 했을 때만 60초 동안 advertising을 엽니다.

현재 `BluetoothRgbPeripheral::start()`는 Bluetooth stack만 초기화하고 advertising은 닫아둡니다.

```cpp
int start()
{
	k_work_init_delayable(&advertising_timeout_work_,
			      BluetoothRgbPeripheral::advertising_timeout_entry);

	int ret = bt_enable(nullptr);
	...

	printf("Bluetooth advertising is closed. Long press the button to open it for %d seconds.\n",
	       kBleAdvertisingWindowMs / 1000);
	return 0;
}
```

버튼 long press가 들어오면 LED를 끄고 advertising window를 엽니다.

```cpp
if (held_ms >= kLongPressMs) {
	color_index = 0;
	(void)blinker.off();
	bluetooth.open_advertising_window();
	printf("Long press: LED off and Bluetooth advertising open\n");
	return;
}
```

`open_advertising_window()`는 advertising을 시작하고 60초 timeout work를 예약합니다.

```cpp
void open_advertising_window()
{
	advertising_window_open_ = true;

	int ret = start_advertising();
	...

	(void)k_work_reschedule(&advertising_timeout_work_,
				K_MSEC(kBleAdvertisingWindowMs));
}
```

60초가 지나면 `advertising_timeout_entry()`가 호출되고, 이 함수가 다시 객체를 찾아 window를 닫습니다.

```cpp
static void advertising_timeout_entry(k_work *work)
{
	auto *delayable = k_work_delayable_from_work(work);
	auto *self = CONTAINER_OF(delayable,
				  BluetoothRgbPeripheral,
				  advertising_timeout_work_);

	self->close_advertising_window();
}
```

여기서는 `k_work_delayable`이 객체 멤버이므로 `CONTAINER_OF` 패턴을 사용합니다.

처음 부팅 후 흐름:

```text
bt_enable()
 -> advertising off
 -> 폰에서 RGB Button이 보이지 않음
```

버튼 long press 후 흐름:

```text
버튼 long press
 -> LED off
 -> start_advertising()
 -> 60초 동안 폰에서 RGB Button 발견 가능
```

연결이 끊어진 뒤에는 60초 window가 아직 열려 있을 때만 advertising을 다시 시작합니다.

```cpp
static void recycled_entry()
{
	if (instance_ != nullptr) {
		instance_->handle_recycled();
	}
}
```

```cpp
void handle_recycled()
{
	if (!advertising_window_open_) {
		return;
	}

	int ret = start_advertising();
	...
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
부팅
 -> Bluetooth stack init
 -> advertising off

버튼 long press
 -> 60초 advertising window open
 -> RGB Button 스캔 가능

폰 연결
 -> advertising 멈춤

폰 연결 해제
 -> disconnected_entry() 로그 출력
 -> Zephyr가 connection object 정리
 -> recycled_entry() 호출

60초 window가 아직 열려 있으면
 -> start_advertising()
 -> 다시 RGB Button으로 스캔/연결 가능

60초 window가 닫혔으면
 -> advertising 재시작 안 함
```

테스트할 때 monitor에서 기대할 수 있는 로그:

```text
Bluetooth initialized
Bluetooth advertising is closed. Long press the button to open it for 60 seconds.
Long press: LED off and Bluetooth advertising open
Bluetooth advertising as "RGB Button"
Bluetooth connected
Bluetooth disconnected: 0x13
Bluetooth advertising as "RGB Button"
Bluetooth advertising window closed
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

## SYS_PORT_TRACING_OBJ_FUNC_ENTER 매크로

선택한 코드는 Zephyr tracing 기능이 꺼져 있을 때의 기본 정의입니다.

```cpp
#define SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func, obj, ...) do { } while (false)
```

뜻은 다음과 같습니다.

```text
추적 기능이 꺼져 있으면
  이 매크로가 호출되어도 아무 것도 하지 않음

추적 기능이 켜져 있으면
  함수 진입 시점 trace 이벤트를 기록
```

예를 들어 `zephyr/kernel/work.c`에는 이런 코드가 있습니다.

```cpp
SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_work, submit, work);
```

tracing이 꺼져 있으면 전처리 후 개념적으로 이렇게 됩니다.

```cpp
do { } while (false);
```

즉 실행되는 일은 없습니다.

### do { } while (false)를 쓰는 이유

그냥 빈 매크로로 만들 수도 있을 것 같지만, Zephyr는 아래처럼 씁니다.

```cpp
do { } while (false)
```

이 패턴은 매크로를 일반 함수 호출처럼 안전하게 쓰기 위한 C/C++ 관용구입니다.

예:

```cpp
if (ready)
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_work, submit, work);
else
	do_something();
```

매크로가 `do { } while (false)`이면 문장 하나처럼 동작하므로 `if/else` 구조가 깨지지 않습니다.

```text
do
  빈 블록 실행
while (false)
  한 번만 실행하고 끝
```

결과적으로 아무 것도 하지 않지만, 문법적으로는 안전한 하나의 statement가 됩니다.

### obj_type, func, obj, ... 의미

```cpp
SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func, obj, ...)
```

각 인자는 다음 의미입니다.

```text
obj_type
  추적 대상 객체 타입
  예: k_work, k_work_queue, k_sem, k_mutex

func
  어떤 함수/동작에 들어가는지 이름
  예: submit, reschedule, cancel

obj
  실제 대상 객체 포인터
  예: work, queue, sem

...
  추가 정보
  예: timeout, delay, return value에 필요한 값 등
```

`...`는 variadic macro입니다. 인자를 더 받을 수 있다는 뜻입니다.

예:

```cpp
SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_work, submit, work);
SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_work, reschedule_for_queue, queue, dwork, delay);
```

둘 다 같은 매크로로 받을 수 있습니다.

### tracing이 켜져 있을 때

`CONFIG_TRACING`이 켜져 있으면 같은 매크로가 빈 동작이 아니라 실제 trace 호출로 정의됩니다.

```cpp
#define SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func, obj, ...) \
	do { \
		SYS_PORT_TRACING_TYPE_MASK(obj_type, \
			_SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func)(obj, ##__VA_ARGS__)); \
	} while (false)
```

개념적으로는 다음처럼 바뀝니다.

```text
SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_work, submit, work)
 -> k_work 타입 trace가 켜져 있는지 확인
 -> sys_port_trace_k_work_submit_enter(work) 같은 함수 호출
```

즉 이 매크로는 kernel 코드 곳곳에 trace hook 자리를 심어두는 역할입니다.

### 현재 앱 입장에서의 의미

현재 `my_app`을 이해하는 데 직접 필요한 코드는 아닙니다. `k_work_reschedule()`이나 workqueue 내부를 따라가다 보면 Zephyr kernel 구현 안에서 보이는 tracing hook입니다.

정리하면:

```text
CONFIG_TRACING off:
  아무 것도 안 함
  런타임 비용 거의 없음

CONFIG_TRACING on:
  kernel 함수 진입/종료 같은 이벤트를 trace backend로 보냄
```

따라서 선택한 줄은 "이 함수가 무언가 중요한 로직을 직접 실행한다"기보다는, "tracing이 꺼져 있으니 이 trace 지점은 빈 코드로 처리한다"는 뜻입니다.

## Zephyr에서 define을 많이 쓰는 이유

Zephyr 코드를 보면 `#define` 매크로가 매우 많이 나옵니다. 단순히 짧은 이름을 만들기 위한 것도 있지만, Zephyr에서는 매크로가 더 중요한 역할을 합니다.

대표 이유:

```text
1. 컴파일 타임에 코드를 생성하기 위해
2. 함수 이름이나 변수 이름을 조합하기 위해
3. 설정에 따라 코드를 완전히 없애기 위해
4. linker section에 객체를 자동 등록하기 위해
5. devicetree/Kconfig 정보를 C 코드로 연결하기 위해
6. C API를 함수처럼 안전하게 쓰기 위해
```

## 1. 컴파일 타임 코드 생성

예를 들어 BLE service 등록 코드는 함수 호출처럼 보이지만 실제로는 구조체 배열을 생성합니다.

```cpp
BT_GATT_SERVICE_DEFINE(rgb_service,
	BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid),
	BT_GATT_CHARACTERISTIC(...),
);
```

개념적으로는 이런 코드를 만들어냅니다.

```cpp
const struct bt_gatt_attr attr_rgb_service[] = {
	...
};

const struct bt_gatt_service_static rgb_service = {
	.attrs = attr_rgb_service,
	.attr_count = ARRAY_SIZE(attr_rgb_service),
};
```

즉 런타임에 "서비스를 하나 만들어 주세요"라고 동적으로 등록하는 것이 아니라, 컴파일 시점에 service 구조체를 만들어둡니다.

임베디드에서는 이런 방식이 유리합니다.

```text
동적 할당이 줄어듦
초기화 순서가 명확해짐
RAM 사용량을 예측하기 쉬움
런타임 비용이 줄어듦
```

## 2. 이름 조합

Zephyr 매크로는 함수 이름이나 변수 이름을 조합하기도 합니다.

예:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks)
```

매크로 내부에서는 대략 이런 이름을 만듭니다.

```text
bt_conn_cb_ + connection_callbacks
 -> bt_conn_cb_connection_callbacks
```

실제 정의:

```cpp
#define BT_CONN_CB_DEFINE(_name) \
	static const STRUCT_SECTION_ITERABLE(bt_conn_cb, \
					     _CONCAT(bt_conn_cb_, _name))
```

`_CONCAT()` 같은 매크로가 토큰을 붙여서 새 이름을 만듭니다.

tracing 매크로도 비슷합니다.

```cpp
SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_work, submit, work)
```

tracing이 켜져 있으면 내부적으로 이런 이름을 만들 수 있습니다.

```text
sys_port_trace_ + k_work + _ + submit + _enter
 -> sys_port_trace_k_work_submit_enter
```

이런 방식으로 같은 매크로 하나가 여러 객체 타입/함수 이름에 맞는 trace 함수를 생성하거나 호출할 수 있습니다.

## 3. 설정에 따라 코드 제거

Zephyr는 Kconfig 설정에 따라 기능이 켜지고 꺼집니다.

예:

```cpp
#if !defined(CONFIG_TRACING)
#define SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func, obj, ...) do { } while (false)
#else
#define SYS_PORT_TRACING_OBJ_FUNC_ENTER(obj_type, func, obj, ...) ...
#endif
```

`CONFIG_TRACING`이 꺼져 있으면 trace 매크로는 빈 코드가 됩니다.

```cpp
do { } while (false)
```

결과:

```text
소스 코드에는 trace hook이 남아 있음
tracing을 끄면 실제 실행 코드는 사라짐
런타임 비용이 거의 없음
```

이 방식은 임베디드에서 중요합니다. 기능이 꺼져 있는데도 코드와 메모리를 차지하면 안 되기 때문입니다.

## 4. linker section 자동 등록

Zephyr는 특정 객체들을 특별한 linker section에 넣고, 부팅 중 자동으로 찾아서 사용합니다.

예:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
};
```

이 코드는 단순 전역 변수를 만드는 것처럼 보이지만, 실제로는 `STRUCT_SECTION_ITERABLE`을 통해 Zephyr가 순회 가능한 section에 들어갑니다.

개념:

```text
여러 파일에서 callback 객체 정의
 -> 모두 특정 linker section에 모임
 -> Bluetooth stack이 section을 순회
 -> 등록된 callback들을 자동으로 호출
```

그래서 우리가 `bt_conn_cb_register()`를 직접 호출하지 않아도 callback이 등록됩니다.

## 5. devicetree 정보를 C 코드로 연결

현재 코드의 이 부분도 매크로입니다.

```cpp
#define SW0_NODE DT_ALIAS(sw0)
#define STRIP_NODE DT_ALIAS(led_strip)

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
```

이 코드는 devicetree에 있는 `sw0`, `led_strip` 정보를 C 코드로 가져옵니다.

즉:

```text
보드/overlay에 정의된 하드웨어 정보
 -> devicetree generated header
 -> DT_* 매크로
 -> C/C++ 코드에서 device/spec로 사용
```

Zephyr가 다양한 보드를 지원할 수 있는 핵심 방식입니다.

## 6. 함수처럼 안전하게 쓰는 매크로

아래처럼 `do { } while (false)`를 쓰는 매크로도 많습니다.

```cpp
#define SOME_MACRO(...) do { \
	... \
} while (false)
```

이렇게 하면 매크로가 문장 하나처럼 동작합니다.

```cpp
if (ready)
	SOME_MACRO();
else
	do_other();
```

`if/else` 구조에서 문법이 깨지지 않습니다.

## 함수 대신 define을 쓰는 이유

함수로 만들면 좋은 경우도 많지만, 아래 경우는 매크로가 필요하거나 유리합니다.

```text
1. 타입 이름, 함수 이름, 변수 이름 자체를 조합해야 할 때
2. 컴파일 타임에 구조체/배열을 선언해야 할 때
3. 설정이 꺼져 있으면 코드를 완전히 없애야 할 때
4. devicetree 값을 컴파일 타임 상수로 써야 할 때
5. 여러 타입에 대해 같은 패턴의 코드를 생성해야 할 때
```

반대로 일반 로직은 가능하면 함수나 class 멤버 함수가 더 읽기 좋습니다.

## 현재 my_app에서 볼 수 있는 매크로 예

```text
DT_ALIAS
  devicetree alias 가져오기

GPIO_DT_SPEC_GET
  devicetree에서 GPIO spec 만들기

DEVICE_DT_GET
  devicetree node에서 device 포인터 가져오기

BT_DATA / BT_DATA_BYTES
  BLE advertising data 구조체 만들기

BT_UUID_INIT_128 / BT_UUID_128_ENCODE
  BLE UUID 구조체 초기화

BT_CONN_CB_DEFINE
  BLE connection callback을 linker section에 등록

BT_GATT_SERVICE_DEFINE
  GATT service 구조체와 attribute 배열 생성/등록

BT_GATT_CHARACTERISTIC
  characteristic declaration + value attribute 생성

SHELL_CMD_REGISTER
  shell 명령 등록

CONTAINER_OF
  멤버 포인터에서 바깥 객체 포인터 계산

ARRAY_SIZE
  배열 요소 개수 계산
```

## 읽을 때의 요령

Zephyr 매크로를 볼 때는 바로 모든 내부를 외우려고 하지 말고, 먼저 역할을 분류하면 됩니다.

```text
이 매크로가 하는 일이 무엇인가?

1. 객체를 선언/등록하는가?
   예: BT_GATT_SERVICE_DEFINE, BT_CONN_CB_DEFINE, SHELL_CMD_REGISTER

2. 하드웨어 정보를 가져오는가?
   예: DT_ALIAS, GPIO_DT_SPEC_GET, DEVICE_DT_GET

3. 이름을 만들어내는가?
   예: _CONCAT, tracing 매크로

4. 설정에 따라 없어지는 코드인가?
   예: SYS_PORT_TRACING_*

5. 포인터 계산/배열 계산 helper인가?
   예: CONTAINER_OF, ARRAY_SIZE
```

이렇게 보면 매크로가 많아도 훨씬 덜 복잡하게 읽을 수 있습니다.
