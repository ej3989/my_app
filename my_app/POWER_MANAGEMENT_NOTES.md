# Power Management Notes

이 문서는 Zephyr와 ESP32-S3에서 sleep, deep sleep, wakeup을 이해하기 위한 메모입니다.

## 핵심 요약

deep sleep는 "잠깐 멈췄다가 이어서 실행"이 아니라, 거의 꺼졌다가 wakeup 조건으로 다시 부팅되는 상태에 가깝습니다.

현재 앱 기준으로 보면:

- BLE advertising은 deep sleep 동안 유지되지 않습니다.
- BLE 연결도 deep sleep에 들어가면 끊어진다고 봐야 합니다.
- `k_work_delayable` 같은 workqueue 타이머는 deep sleep 중에 계속 카운트되는 일반 타이머처럼 생각하면 안 됩니다.
- 일반 RAM의 C++ 객체 상태는 보존되지 않습니다.
- wakeup 후에는 `main()`부터 다시 초기화되는 흐름으로 설계해야 합니다.
- 유지해야 할 값은 NVS, flash, RTC memory 같은 별도 저장 위치를 고려해야 합니다.

## Zephyr의 power state 이름

Zephyr는 전원 상태를 `enum pm_state`로 표현합니다.

관련 파일:

- `zephyr/include/zephyr/pm/state.h`

중요한 상태:

```cpp
PM_STATE_SUSPEND_TO_IDLE
PM_STATE_STANDBY
PM_STATE_SUSPEND_TO_RAM
PM_STATE_SOFT_OFF
```

이 중 ESP32-S3에서 실제 deep sleep으로 이어지는 쪽은 `PM_STATE_SOFT_OFF`입니다.

`state.h` 설명상 `PM_STATE_SOFT_OFF`는 CPU와 memory 내용이 보존되지 않고, 다시 켜질 때 초기 부팅처럼 시작됩니다.

즉 deep sleep을 쓰면 이런 생각을 해야 합니다.

```text
버튼 길게 누름
-> deep sleep 진입
-> 보드가 거의 꺼짐
-> wakeup 조건 발생
-> reset/boot와 비슷하게 다시 시작
-> main() 다시 실행
```

## ESP32-S3에서 light sleep과 deep sleep

ESP32 계열 Zephyr SoC 코드는 다음 파일에 있습니다.

- `zephyr/soc/espressif/common/power.c`
- `zephyr/soc/espressif/common/poweroff.c`

`power.c` 안의 `pm_state_set()` 흐름을 보면 상태에 따라 Espressif sleep API가 호출됩니다.

개념적으로는 다음과 같습니다.

```cpp
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
    switch (state) {
    case PM_STATE_STANDBY:
        esp_light_sleep_start();
        break;

    case PM_STATE_SOFT_OFF:
        esp_deep_sleep_start();
        break;
    }
}
```

실제 코드는 wakeup source 설정, GPIO hold 설정, RTC 설정 등이 더 들어갑니다.

정리하면:

- `PM_STATE_STANDBY`: ESP32 light sleep 쪽
- `PM_STATE_SOFT_OFF`: ESP32 deep sleep 쪽

## `sys_poweroff()`와 deep sleep

Zephyr에는 시스템을 꺼진 상태로 보내는 API가 있습니다.

관련 파일:

- `zephyr/include/zephyr/sys/poweroff.h`
- `zephyr/soc/espressif/common/poweroff.c`

ESP32-S3 구현에서는 `z_sys_poweroff()` 안에서 최종적으로 `esp_deep_sleep_start()`를 호출합니다.

흐름은 대략 이렇습니다.

```cpp
void z_sys_poweroff(void)
{
    // RTC domain, ULP wakeup, GPIO sleep hold 준비
    esp_deep_sleep_start();
}
```

그래서 ESP32-S3에서 `sys_poweroff()`는 사실상 deep sleep 진입 방법 중 하나로 볼 수 있습니다.

주의할 점:

- `sys_poweroff()`는 돌아오지 않는 함수입니다.
- 호출한 뒤 다음 줄이 실행된다고 생각하면 안 됩니다.
- wakeup 후에는 다시 부팅 흐름으로 들어갑니다.

## Wakeup source

deep sleep에 들어가기 전에 반드시 "무엇으로 깨어날지"를 정해야 합니다.

대표 wakeup source:

- 타이머
- GPIO 버튼
- ULP/LP core
- 일부 RTC peripheral

Zephyr 문서에서는 wakeup 가능한 장치를 devicetree에서 `wakeup-source`로 표시하고, 애플리케이션에서 `pm_device_wakeup_enable()`로 활성화하는 흐름을 설명합니다.

관련 문서:

- `zephyr/doc/services/pm/device.rst`

예상 흐름:

```text
devicetree에서 wakeup-source 표시
-> 앱에서 pm_device_wakeup_enable(device, true)
-> sleep/deep sleep 진입
-> wakeup source 이벤트 발생
-> 보드 wakeup
```

ESP32 deep sleep은 wakeup source 설정이 특히 중요합니다.

`power.c` 주석에도 다음 의미의 구분이 있습니다.

```text
Light sleep:
  devicetree에 wakeup 가능하다고 선언되어 있으면 충분한 경우가 있음

Deep sleep:
  PM policy에서 wakeup이 명시적으로 enable되어야 함
```

## 타이머 wakeup

타이머도 wakeup source로 사용할 수 있습니다.

ESP32-S3에서는 deep sleep에 들어가기 전에 RTC timer wakeup을 설정해 두면, 지정한 시간이 지난 뒤 보드가 다시 깨어납니다.

Zephyr의 ESP32 공통 power 코드에서도 RTC timer wakeup을 설정하는 흐름이 있습니다.

관련 파일:

- `zephyr/soc/espressif/common/power.c`

핵심적으로는 이런 Espressif API가 사용됩니다.

```cpp
esp_sleep_enable_timer_wakeup(...)
```

의미는 다음과 같습니다.

```text
deep sleep 들어가기 전에
-> "몇 us 뒤에 깨워라" 설정
-> esp_deep_sleep_start()
-> 시간이 지나면 wakeup
-> 부팅 흐름으로 다시 시작
```

주의할 점:

- 타이머 wakeup 후에도 기존 함수 위치로 돌아오는 것이 아닙니다.
- deep sleep wakeup은 일반적으로 reset/boot와 비슷하게 `main()`부터 다시 시작한다고 생각해야 합니다.
- 타이머 wakeup으로 깨어난 뒤 무엇을 할지는 앱 시작 시점에서 판단해야 합니다.

예를 들어 5분마다 깨어나서 BLE advertising을 잠깐 여는 장치를 만들면 흐름은 이렇습니다.

```text
main()
-> wakeup reason 확인
-> BLE advertising 60초 open
-> 필요한 작업 수행
-> 다음 timer wakeup 설정
-> deep sleep 진입
```

현재 앱에 적용한다면 후보 흐름은 다음과 같습니다.

```text
long press
-> BLE advertising 60초 open
-> 연결 안 되면 advertising stop
-> 10분 timer wakeup 설정
-> deep sleep
-> 10분 뒤 wakeup
-> main() 재시작
-> 다시 대기 또는 advertising open
```

버튼 wakeup과 타이머 wakeup을 같이 쓰는 것도 가능합니다.

예:

```text
deep sleep
-> 버튼을 누르면 즉시 wakeup
또는
-> 버튼을 안 눌러도 10분 뒤 timer wakeup
```

이렇게 하면 사용자가 버튼으로 직접 깨울 수도 있고, 장치가 주기적으로 깨어나서 상태를 광고할 수도 있습니다.

## 현재 BLE 앱에 적용하면 생기는 변화

현재 앱은 버튼 long press 때 60초 동안 BLE advertising을 엽니다.

현재 흐름:

```text
long press
-> LED off
-> BLE advertising 60초 open
-> 60초 후 advertising stop
```

deep sleep을 추가하면 후보 흐름은 두 가지입니다.

### 1. 60초 advertising 후 deep sleep

```text
long press
-> LED off
-> BLE advertising 60초 open
-> 60초 동안 폰 연결 가능
-> 연결 안 되면 advertising stop
-> deep sleep 진입
-> 버튼 또는 타이머로 wakeup
-> main()부터 다시 시작
```

이 방식은 배터리 절약에 좋습니다.

단점:

- deep sleep 후 앱 상태가 사라집니다.
- BLE 연결이 유지되지 않습니다.
- wakeup 후 다시 advertising을 열지 말지 정책을 정해야 합니다.

### 2. long press 즉시 deep sleep

```text
long press
-> LED off
-> deep sleep 진입
-> 버튼으로 wakeup
-> main()부터 다시 시작
```

이 방식은 단순하지만 BLE 설정을 바꿀 수 있는 시간이 없습니다.

현재 앱 목표에는 1번 방식이 더 자연스럽습니다.

## workqueue와 deep sleep

현재 코드에는 이런 구조가 있습니다.

```cpp
k_work_delayable advertising_timeout_work_;
```

이 work는 "60초 후 advertising을 닫기" 위해 사용됩니다.

일반 실행 중에는 Zephyr system workqueue가 시간을 보고 callback을 실행합니다.

하지만 deep sleep에 들어가면 CPU와 일반 실행 컨텍스트가 유지되지 않습니다.

따라서 deep sleep 중에는 다음처럼 생각하면 안 됩니다.

```text
deep sleep 중에도 advertising_timeout_work_가 계속 살아 있다
deep sleep에서 깨면 예약된 work가 이어서 실행된다
```

대신 deep sleep 설계는 이렇게 해야 합니다.

```text
sleep 들어가기 전에 필요한 상태 저장
-> deep sleep
-> wakeup
-> main() 재시작
-> 저장된 상태를 보고 복구할 것만 복구
```

## BLE와 deep sleep

BLE는 deep sleep과 같이 유지되지 않습니다.

현재 앱에서 deep sleep 진입 전 해야 할 일:

```text
bt_le_adv_stop()
필요하면 연결 disconnect
필요한 상태 저장
deep sleep 진입
```

deep sleep 후 wakeup되면:

```text
main()
-> LED 초기화
-> 버튼 초기화
-> BluetoothRgbPeripheral::start()
-> 필요할 때 advertising 다시 open
```

즉 BLE는 "잠들었다가 이어서 연결 유지"가 아니라 "다시 초기화 후 새로 광고/연결"입니다.

## 현재 앱에서 다음 구현 후보

현재 앱에 deep sleep을 붙인다면 추천 순서는 다음입니다.

1. 먼저 `status` 명령에 현재 전원 정책 설명을 추가
2. `sleep` shell 명령을 만들어 수동으로 deep sleep 테스트
3. wakeup source를 버튼으로 설정
4. long press 동작을 `advertising 60초 후 deep sleep`으로 변경
5. wakeup 후 바로 advertising을 열지, 버튼을 다시 길게 눌러야 열지 정책 결정

처음부터 long press에 바로 붙이면 디버깅이 어렵습니다.

먼저 shell 명령으로 수동 진입을 확인하는 게 안전합니다.

## 기억할 문장

```text
light sleep은 멈췄다가 이어지는 느낌이고,
deep sleep은 꺼졌다가 wakeup으로 다시 부팅되는 느낌이다.
```

현재 BLE 앱에서는 deep sleep을 "BLE 연결 유지 기능"으로 보면 안 되고, "배터리를 아끼기 위해 BLE를 완전히 쉬게 하는 기능"으로 봐야 합니다.
