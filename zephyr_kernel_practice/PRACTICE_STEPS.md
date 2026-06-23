# 실습 단계

`src/main.c`를 수정하면서 이 파일을 체크리스트처럼 사용하세요.

## 1. Thread

먼저 아래처럼 설정합니다.

```c
#define PRACTICE_EXERCISE 1
```

`run_thread_demo()`와 `thread_counter()`를 먼저 읽어보세요.

실행하기 전에 먼저 예상해보세요.

- `[main]`, `[thread A]`, `[thread B]` 중 어느 출력이 먼저 나올까요?
- `THREAD_PRIORITY` 값을 낮추거나 높이면 무엇이 달라질까요?
- 한 thread는 100 ms, 다른 thread는 700 ms 동안 sleep하게 만들면 출력 순서가 어떻게 바뀔까요?

직접 수정해볼 것:

- 반복 횟수를 5에서 10으로 바꿔보세요.
- thread A와 thread B의 sleep 시간을 서로 다르게 만들어보세요.

## 2. Semaphore

`PRACTICE_EXERCISE`를 `2`로 바꿉니다.

`run_semaphore_demo()`와 `sem_waiter()`를 읽어보세요.

생각해볼 질문:

- waiter thread는 왜 `k_sem_take()`에서 멈춰 있을까요?
- `K_FOREVER`를 `K_MSEC(200)`으로 바꾸면 어떤 일이 생길까요?

직접 수정해볼 것:

- `main`이 waiter 출력보다 더 빠르게 semaphore를 주도록 바꿔보세요.
- semaphore limit을 `1`에서 `3`으로 바꾸고 동작 차이를 관찰해보세요.

## 3. Message Queue

`PRACTICE_EXERCISE`를 `3`으로 바꿉니다.

`msgq_producer()`와 `msgq_consumer()`를 읽어보세요.

생각해볼 질문:

- queue가 비어 있으면 어느 쪽이 멈출까요?
- queue가 가득 차면 어느 쪽이 멈출까요?

직접 수정해볼 것:

- `K_MSGQ_DEFINE(..., 8, 4)`에서 queue 길이를 더 작게 바꿔보세요.
- producer가 consumer보다 더 빠르게 동작하도록 바꿔보세요.

## 4. Immediate Work

`PRACTICE_EXERCISE`를 `4`로 바꿉니다.

`fake_gpio_isr_event()`와 `immediate_work_handler()`를 읽어보세요.

생각해볼 질문:

- 어느 함수가 ISR 쪽 역할을 하고 있나요?
- 어느 함수가 ISR에서 미룬 일을 thread context에서 처리하고 있나요?

직접 수정해볼 것:

- sleep 없이 `fake_gpio_isr_event()`를 세 번 연속 호출해보세요.
- work handler가 한 번 실행되는지, 여러 번 실행되는지 관찰해보세요.

## 5. Delayable Work

`PRACTICE_EXERCISE`를 `5`로 바꿉니다.

`delayable_work_handler()`를 읽어보세요.

생각해볼 질문:

- handler가 자기 자신을 다시 예약할 수 있는 이유는 무엇일까요?
- 이 구조가 XPT2046 터치 드라이버의 20 ms drag polling과 어떤 점에서 비슷할까요?

직접 수정해볼 것:

- delay를 300 ms에서 1000 ms로 바꿔보세요.
- 5번이 아니라 10번 실행된 뒤 멈추도록 바꿔보세요.

## 6. Timer

`PRACTICE_EXERCISE`를 `6`으로 바꿉니다.

`timer_expiry_handler()`와 `timer_work_handler()`를 읽어보세요.

생각해볼 질문:

- timer callback은 왜 모든 일을 직접 하지 않고 work를 submit할까요?
- `k_timer`와 `k_work_delayable`은 어떤 점이 다를까요?

직접 수정해볼 것:

- timer 주기를 500 ms에서 100 ms로 바꿔보세요.
- 4초 동안 sleep하는 방식 대신, 10번 실행된 뒤 timer를 멈추도록 바꿔보세요.

## 7. Mutex

`PRACTICE_EXERCISE`를 `7`로 바꿉니다.

`run_mutex_demo()`와 `mutex_counter_thread()`를 읽어보세요.

생각해볼 질문:

- `shared_counter`는 왜 공유 자원인가요?
- `k_mutex_lock()`과 `k_mutex_unlock()` 사이에는 어떤 코드가 들어가야 할까요?
- `k_sleep(K_MSEC(100))`이 lock 안에 있을 때와 lock 밖에 있을 때 차이는 무엇일까요?

직접 수정해볼 것:

- `run_mutex_demo()` 안의 `use_mutex`를 `true`에서 `false`로 바꿔보세요.
- `use_mutex=false`일 때 최종 `shared_counter`가 항상 10이 되는지 관찰해보세요.
- 다시 `use_mutex=true`로 돌리고, 두 thread가 같은 값을 동시에 읽는 일이 사라지는지 확인해보세요.

## 확인 방법

처음에는 새 파일을 만들기보다 `src/main.c` 안에서 한 실습 함수만 직접 바꾸는 방식을 추천합니다.
Zephyr 앱은 보통 `main()`이 하나라서, 새 C 파일에 또 `main()`을 만들면 CMake 설정과 entry point 때문에 혼란스러워질 수 있습니다.

추천 흐름:

1. `PRACTICE_EXERCISE` 숫자를 하나 고릅니다.
2. 해당 `run_*_demo()` 함수와 그 아래 helper 함수만 읽습니다.
3. 실행 전에 serial 출력 순서를 직접 예상합니다.
4. 빌드/플래시/모니터로 실제 출력을 확인합니다.
5. 이 문서의 “직접 수정해볼 것” 중 하나만 바꿉니다.
6. 다시 실행해서 예상과 실제가 어떻게 달라졌는지 비교합니다.

나중에 익숙해지면 `src/main.c`에서 실습 하나를 직접 지우고 처음부터 다시 작성해보는 방식이 좋습니다.
그 단계에서는 완성본을 옆에 열어두되, 먼저 직접 써보고 막히는 부분만 비교하세요.
