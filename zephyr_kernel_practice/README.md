# Zephyr Kernel Practice

Zephyr kernel 기본기를 직접 수정하고 실행하면서 익히기 위한 작은 실습 앱입니다.
한 번에 하나의 개념만 켜고, 출력 순서를 예측한 뒤 실제 동작과 비교하는 방식으로 진행합니다.

## 사용 방법

실습 번호는 두 가지 방식으로 고를 수 있습니다.

첫 번째 방식은 `src/main.c`의 기본값을 바꾸는 것입니다.

```c
#define PRACTICE_EXERCISE 1
```

두 번째 방식은 빌드할 때 CMake 값으로 넘기는 것입니다.

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu EJ_APP/zephyr_kernel_practice -- -DPRACTICE_EXERCISE=3
```

`-DPRACTICE_EXERCISE=3`처럼 넘긴 값이 있으면 `src/main.c`의 기본값보다 우선 적용됩니다.

그 다음 Zephyr workspace root에서 플래시와 모니터를 실행합니다.

```sh
west flash
west espressif monitor
```

## 실습 순서

1. Thread 생성과 `k_sleep()`
2. 두 thread 사이의 semaphore handoff
3. Message queue producer/consumer
4. `k_work_submit()`으로 즉시 workqueue에 넘기기
5. `k_work_reschedule()`을 쓰는 delayable work
6. Timer callback에서 workqueue로 일 넘기기
7. Mutex로 공유 변수 보호하기

각 실습마다 먼저 선택된 `run_*_demo()` 함수를 읽고, serial 출력 순서를 예상한 뒤 실행해서 비교하세요.
