# Zephyr Devicetree Device Macro Expansion

이 문서는 현재 `lvgl_practice` 프로젝트의 WS2812 I2S 노드를 예제로 다음 흐름을 설명한다.

```text
DT_INST_FOREACH_STATUS_OKAY()
        -> driver definition macro
        -> DEVICE_DT_INST_DEFINE()
        -> struct device 생성
        -> 부팅 시 driver init
        -> DEVICE_DT_GET()으로 참조
```

현재 빌드에서 WS2812 device의 dependency ordinal은 `114`이다. Ordinal과 생성 symbol은 Devicetree 또는 빌드 구성이 바뀌면 달라질 수 있다.

## 1. Devicetree 노드

애플리케이션 overlay에는 다음과 같은 WS2812 노드가 있다.

```dts
&i2s0 {
	status = "okay";

	led_strip: ws2812@0 {
		compatible = "worldsemi,ws2812-i2s";
		reg = <0>;
		chain-length = <1>;
		color-mapping = <LED_COLOR_ID_GREEN
				 LED_COLOR_ID_RED
				 LED_COLOR_ID_BLUE>;
		reset-delay = <500>;
	};
};
```

드라이버는 `compatible` 값으로 자신이 처리할 노드를 선택한다.

## 2. Driver compatible 지정

파일:

```text
zephyr/drivers/led_strip/ws2812_i2s.c
```

드라이버 상단에는 다음 정의가 있다.

```c
#define DT_DRV_COMPAT worldsemi_ws2812_i2s
```

Devicetree compatible 문자열은 C token으로 변환된다.

```text
"worldsemi,ws2812-i2s"
            ->
worldsemi_ws2812_i2s
```

이후 `DT_INST_*` 매크로는 모두 `DT_DRV_COMPAT`에 해당하는 노드를 대상으로 한다.

## 3. Generated header의 instance 정보

빌드 후 생성되는 파일:

```text
build/zephyr/include/generated/zephyr/devicetree_generated.h
```

현재 빌드에는 다음 매크로가 생성되어 있다.

```c
#define DT_N_INST_0_worldsemi_ws2812_i2s \
	DT_N_S_soc_S_i2s_6000f000_S_ws2812_0
```

의미는 다음과 같다.

```text
worldsemi,ws2812-i2s compatible의 0번 instance
        -> /soc/i2s@6000f000/ws2812@0
```

활성화된 instance를 순회하는 매크로도 생성된다.

```c
#define DT_FOREACH_OKAY_INST_worldsemi_ws2812_i2s(fn) fn(0)
```

현재 활성화된 노드가 하나이므로 `fn(0)`만 생성된다. 같은 compatible의 활성 노드가 두 개라면 개념적으로 다음과 같이 생성된다.

```c
#define DT_FOREACH_OKAY_INST_worldsemi_ws2812_i2s(fn) \
	fn(0) fn(1)
```

## 4. DT_INST_FOREACH_STATUS_OKAY 확장

드라이버 마지막에는 다음 코드가 있다.

```c
DT_INST_FOREACH_STATUS_OKAY(WS2812_I2S_DEVICE)
```

Zephyr의 매크로 정의는 다음과 같다.

```c
#define DT_INST_FOREACH_STATUS_OKAY(fn)                        \
	COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT),  \
		    (UTIL_CAT(DT_FOREACH_OKAY_INST_,           \
			      DT_DRV_COMPAT)(fn)),              \
		    ())
```

현재 값을 대입하면 다음 순서로 확장된다.

```text
DT_INST_FOREACH_STATUS_OKAY(WS2812_I2S_DEVICE)

-> DT_FOREACH_OKAY_INST_worldsemi_ws2812_i2s(
       WS2812_I2S_DEVICE)

-> WS2812_I2S_DEVICE(0)
```

`DT_INST_FOREACH_STATUS_OKAY()`는 device를 직접 만드는 매크로가 아니다. 활성 instance마다 전달받은 매크로를 호출하는 반복 매크로다.

## 5. WS2812_I2S_DEVICE 확장

드라이버의 정의 매크로는 다음 작업을 묶어서 수행한다.

```c
#define WS2812_I2S_DEVICE(idx)                             \
	K_MEM_SLAB_DEFINE_STATIC(...);                     \
	static const uint8_t color_mapping[] = ...;        \
	static const struct ws2812_i2s_cfg config = {...}; \
	DEVICE_DT_INST_DEFINE(...);
```

`idx`에 `0`이 들어가면 개념적으로 다음 코드가 생성된다.

```c
K_MEM_SLAB_DEFINE_STATIC(ws2812_i2s_0_slab,
			 WS2812_I2S_BUFSIZE(0), 2, 4);

static const uint8_t ws2812_i2s_0_color_mapping[] =
	DT_INST_PROP(0, color_mapping);

static const struct ws2812_i2s_cfg ws2812_i2s_0_cfg = {
	.dev = DEVICE_DT_GET(DT_INST_BUS(0)),
	.tx_buf_bytes = WS2812_I2S_BUFSIZE(0),
	.mem_slab = &ws2812_i2s_0_slab,
	.num_colors = WS2812_NUM_COLORS(0),
	.length = DT_INST_PROP(0, chain_length),
	.color_mapping = ws2812_i2s_0_color_mapping,
	/* 나머지 속성 */
};

DEVICE_DT_INST_DEFINE(
	0,
	ws2812_i2s_init,
	NULL,
	NULL,
	&ws2812_i2s_0_cfg,
	POST_KERNEL,
	CONFIG_LED_STRIP_INIT_PRIORITY,
	&ws2812_i2s_api
);
```

한 노드에서 다음 객체가 생성된다.

```text
- I2S 전송용 memory slab
- color mapping 배열
- const config 구조체
- struct device 객체
- 부팅 초기화 entry
```

## 6. DT_INST_PROP 속성 변환

예를 들어 다음 코드는:

```c
DT_INST_PROP(0, chain_length)
```

다음 순서로 값을 찾는다.

```text
0번 worldsemi_ws2812_i2s instance
        -> /soc/i2s@6000f000/ws2812@0
        -> chain-length 속성
        -> 1
```

따라서 config에는 개념적으로 다음 값이 들어간다.

```c
.length = 1;
```

부모 bus도 같은 방식으로 연결된다.

```c
.dev = DEVICE_DT_GET(DT_INST_BUS(0));
```

현재 부모 I2S device의 ordinal은 `113`이므로 개념적으로 다음과 같다.

```c
.dev = &__device_dts_ord_113;
```

## 7. DEVICE_DT_INST_DEFINE 확장

`DEVICE_DT_INST_DEFINE()`은 instance 번호를 node identifier로 바꾼 뒤 `DEVICE_DT_DEFINE()`을 호출한다.

```c
#define DEVICE_DT_INST_DEFINE(inst, ...) \
	DEVICE_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)
```

`DT_DRV_INST(0)`의 확장 과정은 다음과 같다.

```text
DT_DRV_INST(0)

-> DT_INST(0, DT_DRV_COMPAT)

-> DT_INST(0, worldsemi_ws2812_i2s)

-> DT_N_INST_0_worldsemi_ws2812_i2s

-> DT_N_S_soc_S_i2s_6000f000_S_ws2812_0
```

결국 다음 형태가 된다.

```c
DEVICE_DT_DEFINE(
	DT_N_S_soc_S_i2s_6000f000_S_ws2812_0,
	ws2812_i2s_init,
	NULL,
	NULL,
	&ws2812_i2s_0_cfg,
	POST_KERNEL,
	CONFIG_LED_STRIP_INIT_PRIORITY,
	&ws2812_i2s_api
);
```

## 8. Ordinal과 device symbol

Generated header에는 다음 ordinal이 있다.

```c
#define DT_N_S_soc_S_i2s_6000f000_S_ws2812_0_ORD 114
```

기본 Zephyr 빌드는 이 ordinal을 이용해 device symbol 이름을 만든다.

```text
WS2812 node identifier
        -> dependency ordinal 114
        -> dts_ord_114
        -> __device_dts_ord_114
```

최종 link map에는 실제 symbol이 존재한다.

```text
0x3c063868  __device_dts_ord_114
```

주소는 빌드마다 달라질 수 있다.

## 9. struct device 생성

`DEVICE_DT_DEFINE()`은 내부적으로 `Z_DEVICE_DEFINE()`과 `Z_DEVICE_BASE_DEFINE()`까지 확장된다.

개념적으로 다음 전역 객체가 만들어진다.

```c
const struct device __device_dts_ord_114 = {
	.name = "ws2812@0",
	.config = &ws2812_i2s_0_cfg,
	.api = &ws2812_i2s_api,
	.data = NULL,
	.state = &device_state,
	.ops.init = ws2812_i2s_init,
};
```

구조는 다음과 같다.

```text
__device_dts_ord_114
├── name   -> "ws2812@0"
├── config -> ws2812_i2s_0_cfg
│             ├── dev -> __device_dts_ord_113 (I2S)
│             ├── length
│             ├── color_mapping
│             └── mem_slab
├── data   -> NULL
├── api    -> ws2812_i2s_api
│             ├── update_rgb
│             ├── update_channels
│             └── length
└── init   -> ws2812_i2s_init
```

## 10. 부팅 초기화

`DEVICE_DT_DEFINE()`은 초기화 entry도 생성한다.

```text
level: POST_KERNEL
priority: CONFIG_LED_STRIP_INIT_PRIORITY
init function: ws2812_i2s_init
```

부팅 시 Zephyr는 초기화 레벨과 우선순위에 따라 다음과 같이 호출한다.

```c
ws2812_i2s_init(&__device_dts_ord_114);
```

초기화 함수는 `struct device`에서 config를 꺼내 부모 I2S를 설정한다.

```c
static int ws2812_i2s_init(const struct device *dev)
{
	const struct ws2812_i2s_cfg *cfg = dev->config;

	return i2s_configure(cfg->dev, I2S_DIR_TX, &config);
}
```

부모 I2S ordinal `113`이 WS2812 ordinal `114`보다 먼저 배치되는 것은 dependency 순서를 나타낸다.

```text
__device_dts_ord_113  I2S 초기화
          ->
__device_dts_ord_114  WS2812 초기화
```

## 11. DEVICE_DT_GET 연결

애플리케이션의 다음 코드는:

```c
#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_STRIP_DEV DEVICE_DT_GET(LED_STRIP_NODE)
```

개념적으로 다음으로 확장된다.

```c
#define LED_STRIP_DEV (&__device_dts_ord_114)
```

따라서 다음 API 호출은:

```c
led_strip_update_rgb(LED_STRIP_DEV, &pixel, 1);
```

device의 API table을 통해 실제 드라이버 함수로 연결된다.

```text
led_strip_update_rgb()
        -> dev->api
        -> ws2812_i2s_api.update_rgb
        -> ws2812_strip_update_rgb()
```

## 전체 흐름

```text
Devicetree
compatible = "worldsemi,ws2812-i2s"
status = "okay"
        │
        ▼
devicetree_generated.h
DT_FOREACH_OKAY_INST_worldsemi_ws2812_i2s(fn) -> fn(0)
        │
        ▼
DT_INST_FOREACH_STATUS_OKAY(WS2812_I2S_DEVICE)
        │
        ▼
WS2812_I2S_DEVICE(0)
        │
        ├── config 생성
        ├── slab 생성
        ├── API table 연결
        └── DEVICE_DT_INST_DEFINE(0, ...)
                │
                ▼
const struct device __device_dts_ord_114
                │
                ▼
부팅 시 ws2812_i2s_init()
                │
                ▼
DEVICE_DT_GET(DT_ALIAS(led_strip))
                │
                ▼
&__device_dts_ord_114
```

## 핵심 정리

1. `DT_INST_FOREACH_STATUS_OKAY()`는 활성 instance마다 정의 매크로를 호출한다.
2. 전달된 정의 매크로가 config, data, slab 등의 instance별 객체를 만든다.
3. `DEVICE_DT_INST_DEFINE()`이 실제 `struct device`와 초기화 entry를 생성한다.
4. dependency ordinal은 device symbol 이름과 초기화 의존관계를 연결한다.
5. 애플리케이션의 `DEVICE_DT_GET()`은 생성된 device 객체의 주소로 확장된다.
6. 실제 API 호출은 `struct device.api`에 연결된 드라이버 함수로 전달된다.
