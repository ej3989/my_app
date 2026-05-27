# Zephyr Macro And Linker Section Notes

## 자동으로 찾을 수 있는 section에 넣는다는 뜻

Zephyr에서 자주 나오는 말 중 하나가 "객체를 linker section에 넣는다"입니다.

쉽게 말하면:

```text
여러 파일에 흩어져 있는 특정 타입의 전역 객체들을
빌드할 때 한 메모리 구역에 모아두고
Zephyr가 부팅 중 그 구역을 처음부터 끝까지 순회하면서 자동으로 찾는다
```

는 뜻입니다.

## 일반 전역 변수와 차이

보통 C/C++ 전역 변수는 그냥 전역 변수입니다.

```cpp
static int value = 10;
```

이 변수는 이름을 직접 알아야 사용할 수 있습니다.

```cpp
printf("%d\n", value);
```

하지만 Zephyr의 많은 등록 구조는 이름을 직접 부르지 않습니다.

예:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
};
```

우리는 `connection_callbacks`를 직접 어딘가에 넘기지 않았습니다. 그런데도 BLE stack은 이 callback을 알고 있습니다.

그 이유가 linker section입니다.

## BT_CONN_CB_DEFINE 예시

`BT_CONN_CB_DEFINE`는 `conn.h`에 대략 이렇게 정의되어 있습니다.

```cpp
#define BT_CONN_CB_DEFINE(_name) \
	static const STRUCT_SECTION_ITERABLE(bt_conn_cb, \
					     _CONCAT(bt_conn_cb_, _name))
```

이 매크로는 개념적으로 다음과 비슷한 객체를 만듭니다.

```cpp
static const struct bt_conn_cb bt_conn_cb_connection_callbacks = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
};
```

여기서 중요한 부분은 `STRUCT_SECTION_ITERABLE(bt_conn_cb, ...)`입니다.

이 매크로는 이 객체를 일반 전역 위치가 아니라, Zephyr가 `bt_conn_cb` 타입 객체들을 모아두는 특별한 section에 넣습니다.

개념:

```text
section: iterable_bt_conn_cb

  [0] bt_conn_cb_connection_callbacks
  [1] 다른 파일에서 등록한 bt_conn_cb
  [2] 또 다른 bt_conn_cb
```

그 다음 Bluetooth stack은 이 section의 시작과 끝을 알고 순회할 수 있습니다.

```text
for each bt_conn_cb in iterable_bt_conn_cb section:
    연결되면 cb->connected(...)
    끊기면 cb->disconnected(...)
```

그래서 우리가 `bt_conn_cb_register()`를 직접 호출하지 않아도 callback이 등록된 것처럼 동작합니다.

## 왜 이런 방식을 쓰는가

이 방식의 장점은 큽니다.

```text
1. 여러 파일에서 독립적으로 callback/service/driver를 등록할 수 있음
2. 중앙 파일에 목록을 수동으로 추가할 필요가 없음
3. 런타임 동적 할당 없이 컴파일 타임에 등록 구조가 만들어짐
4. 부팅 중 Zephyr가 자동으로 순회 가능
5. 기능별 코드를 느슨하게 연결할 수 있음
```

예를 들어 A 파일, B 파일, C 파일이 각각 callback을 등록한다고 해도 중앙에 이런 목록을 만들 필요가 없습니다.

```cpp
callbacks[] = {
	&a_callback,
	&b_callback,
	&c_callback,
};
```

각 파일에서 그냥 이렇게 쓰면 됩니다.

```cpp
BT_CONN_CB_DEFINE(a_callback) = { ... };
BT_CONN_CB_DEFINE(b_callback) = { ... };
BT_CONN_CB_DEFINE(c_callback) = { ... };
```

빌드 과정에서 linker가 같은 section에 모아줍니다.

## linker가 하는 일

컴파일러는 각 `.c/.cpp` 파일을 object file로 만듭니다.

```text
main.cpp -> main.cpp.obj
gatt.c   -> gatt.c.obj
conn.c   -> conn.c.obj
```

각 object file 안에는 여러 section이 있습니다.

```text
.text      코드
.rodata    읽기 전용 데이터
.data      초기값 있는 전역 변수
.bss       초기값 없는 전역 변수
특수 section들
```

Zephyr 매크로는 어떤 객체에 "이 객체는 이 section에 넣어라"라는 속성을 붙입니다.

그러면 linker가 최종 ELF를 만들 때 같은 이름의 section들을 모읍니다.

```text
main.cpp.obj 안의 iterable_bt_conn_cb
conn.c.obj 안의 iterable_bt_conn_cb
다른 파일의 iterable_bt_conn_cb
  -> 최종 zephyr.elf 안의 하나의 연속된 영역
```

Zephyr는 linker script를 통해 그 영역의 시작/끝 주소를 알 수 있게 만듭니다.

```text
__start_iterable_bt_conn_cb
__stop_iterable_bt_conn_cb
```

그리고 그 사이를 순회합니다.

## 비유

linker section은 "자동으로 모이는 신청서함"처럼 생각하면 됩니다.

```text
각 파일:
  나도 BLE 연결 callback 등록할래
  나도 shell 명령 등록할래
  나도 device driver 등록할래

매크로:
  신청서를 정해진 상자에 넣음

linker:
  같은 상자에 들어간 신청서들을 한 줄로 모음

Zephyr:
  부팅할 때 상자 안의 신청서를 처음부터 끝까지 읽음
```

## my_app에서 관련 있는 예

### BLE connection callback

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
	.recycled = BluetoothRgbPeripheral::recycled_entry,
};
```

이 객체는 `bt_conn_cb` iterable section에 들어갑니다.

### BLE GATT service

```cpp
BT_GATT_SERVICE_DEFINE(rgb_service,
	BT_GATT_PRIMARY_SERVICE(&rgb_service_uuid),
	BT_GATT_CHARACTERISTIC(...),
);
```

이것도 GATT service용 static section에 등록됩니다. 그래서 Bluetooth stack이 GATT service 목록을 알 수 있습니다.

### Shell command

```cpp
SHELL_CMD_REGISTER(rgb, NULL, "Set RGB LED", cmd_rgb);
```

shell 명령도 비슷하게 등록됩니다. shell subsystem이 등록된 명령 목록을 찾을 수 있습니다.

## 중요한 결론

```text
자동으로 찾을 수 있는 section에 넣는다
```

는 말은:

```text
객체 이름을 직접 호출하거나 수동 등록하지 않아도
Zephyr가 특정 section을 순회해서 그 객체를 발견할 수 있게 만든다
```

는 뜻입니다.

그래서 이런 코드가 가능합니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = { ... };
```

별도 등록 함수 호출 없이도 Zephyr BLE stack이 이 callback을 발견합니다.

단, 모든 객체가 자동으로 찾아지는 것은 아닙니다. 반드시 Zephyr가 그런 방식으로 설계한 매크로를 사용해야 합니다.

```text
자동 등록됨:
  BT_CONN_CB_DEFINE
  BT_GATT_SERVICE_DEFINE
  SHELL_CMD_REGISTER
  DEVICE_DT_DEFINE 류

자동 등록 아님:
  일반 static 변수
  일반 class 객체
  일반 함수
```

즉 `static BluetoothRgbPeripheral bluetooth(&blinker);` 자체는 자동 등록되는 것이 아닙니다.  
자동 등록되는 것은 `BT_CONN_CB_DEFINE`, `BT_GATT_SERVICE_DEFINE` 같은 매크로가 만든 객체입니다.

## 실제 Bluetooth connection callback 순회 코드

앞에서 설명한 pseudo code는 다음과 같았습니다.

```text
for each bt_conn_cb in section:
    if connected callback exists:
        callback->connected(conn, err)
```

Zephyr 실제 코드는 `zephyr/subsys/bluetooth/host/conn.c`에 있습니다.

연결이 성립되면 먼저 `bt_conn_connected()`가 호출됩니다.

```c
void bt_conn_connected(struct bt_conn *conn)
{
	schedule_auto_initiated_procedures(conn);
	bt_l2cap_connected(conn);
	notify_connected(conn);
}
```

여기서 실제 app callback 호출은 `notify_connected(conn)` 안에서 일어납니다.

```c
static void notify_connected(struct bt_conn *conn)
{
	BT_CONN_CB_DYNAMIC_FOREACH(callback) {
		if (callback->connected) {
			callback->connected(conn, conn->err);
		}
	}

	STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
		if (cb->connected) {
			cb->connected(conn, conn->err);
		}
	}
}
```

이 코드를 둘로 나눠서 보면 됩니다.

```c
BT_CONN_CB_DYNAMIC_FOREACH(callback) {
	...
}
```

이 부분은 런타임에 `bt_conn_cb_register()`로 등록한 callback 목록을 순회합니다.

그리고:

```c
STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
	...
}
```

이 부분이 `BT_CONN_CB_DEFINE(...)`로 등록한 static callback section을 순회합니다.

우리 코드는 이쪽에 해당합니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
	.recycled = BluetoothRgbPeripheral::recycled_entry,
};
```

따라서 연결 이벤트가 오면 실제 흐름은 다음과 같습니다.

```text
폰이 연결됨
 -> Bluetooth controller/HCI event 처리
 -> bt_conn_connected(conn)
 -> notify_connected(conn)
 -> STRUCT_SECTION_FOREACH(bt_conn_cb, cb)
 -> cb가 우리 connection_callbacks 객체를 가리킴
 -> cb->connected가 nullptr인지 확인
 -> BluetoothRgbPeripheral::connected_entry(conn, conn->err) 호출
```

`if (cb->connected)` 검사는 해당 callback이 등록되어 있는지 확인하는 코드입니다. `struct bt_conn_cb`에는 많은 callback 필드가 있고, 사용하지 않는 필드는 `NULL/nullptr`일 수 있습니다.

`struct bt_conn_cb`의 일부:

```c
struct bt_conn_cb {
	void (*connected)(struct bt_conn *conn, uint8_t err);
	void (*disconnected)(struct bt_conn *conn, uint8_t reason);
	void (*recycled)(void);
	...
};
```

즉 우리 코드가 `.connected`만 넣으면 connected 이벤트만 받고, `.disconnected`를 넣으면 disconnected 이벤트도 받습니다.

## disconnected도 같은 방식

연결 해제도 거의 같습니다.

```c
static void notify_disconnected(struct bt_conn *conn)
{
	BT_CONN_CB_DYNAMIC_FOREACH(callback) {
		if (callback->disconnected) {
			callback->disconnected(conn, conn->err);
		}
	}

	STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
		if (cb->disconnected) {
			cb->disconnected(conn, conn->err);
		}
	}
}
```

흐름:

```text
폰 연결 해제
 -> notify_disconnected(conn)
 -> static bt_conn_cb section 순회
 -> 우리 connection_callbacks 발견
 -> cb->disconnected(conn, conn->err)
 -> BluetoothRgbPeripheral::disconnected_entry(conn, reason)
```

## recycled도 같은 방식

현재 앱은 advertising 재시작 시점을 위해 `.recycled`도 사용합니다.

실제 Zephyr 코드:

```c
static void notify_recycled_conn_slot(void)
{
	BT_CONN_CB_DYNAMIC_FOREACH(callback) {
		if (callback->recycled) {
			callback->recycled();
		}
	}

	STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
		if (cb->recycled) {
			cb->recycled();
		}
	}
}
```

우리 코드:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
	.recycled = BluetoothRgbPeripheral::recycled_entry,
};
```

흐름:

```text
연결 객체가 정리되어 pool로 돌아감
 -> notify_recycled_conn_slot()
 -> static bt_conn_cb section 순회
 -> 우리 connection_callbacks 발견
 -> cb->recycled()
 -> BluetoothRgbPeripheral::recycled_entry()
 -> advertising window가 열려 있으면 advertising 재시작
```

## dynamic callback과 static section callback 차이

Zephyr는 두 종류의 callback 등록을 모두 지원합니다.

### 1. dynamic callback

런타임에 직접 등록합니다.

```c
static struct bt_conn_cb my_cb = {
	.connected = connected,
};

bt_conn_cb_register(&my_cb);
```

이 경우 `BT_CONN_CB_DYNAMIC_FOREACH(callback)`에서 순회됩니다.

### 2. static section callback

컴파일 타임에 section에 넣습니다.

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
};
```

이 경우 `STRUCT_SECTION_FOREACH(bt_conn_cb, cb)`에서 순회됩니다.

현재 `my_app`은 static section callback 방식을 사용합니다.

## 우리 코드와 실제 Zephyr 코드 연결

우리 코드:

```cpp
BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = BluetoothRgbPeripheral::connected_entry,
	.disconnected = BluetoothRgbPeripheral::disconnected_entry,
	.recycled = BluetoothRgbPeripheral::recycled_entry,
};
```

Zephyr 코드:

```c
STRUCT_SECTION_FOREACH(bt_conn_cb, cb) {
	if (cb->connected) {
		cb->connected(conn, conn->err);
	}
}
```

대응 관계:

```text
cb
  -> 우리 connection_callbacks 객체

cb->connected
  -> BluetoothRgbPeripheral::connected_entry

conn
  -> 현재 BLE 연결 객체

conn->err
  -> 연결 성공/실패 또는 해제 이유 코드
```

그래서 최종 호출은 이렇게 됩니다.

```cpp
BluetoothRgbPeripheral::connected_entry(conn, conn->err);
```

이게 "section에 넣어두면 Zephyr가 자동으로 찾는다"의 실제 코드 흐름입니다.
