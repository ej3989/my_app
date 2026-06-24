# 실습 단계

`src/main.c`를 수정하거나 빌드 옵션으로 실습 번호를 바꾸면서 이 파일을 체크리스트처럼 사용하세요.

실습 번호는 아래 두 방식 중 하나로 선택할 수 있습니다.

```c
#define PRACTICE_EXERCISE 1
```

또는 빌드할 때:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu EJ_APP/zephyr_kernel_practice -- -DPRACTICE_EXERCISE=1
```

빌드 옵션으로 넘긴 `-DPRACTICE_EXERCISE=N` 값이 있으면 `src/main.c`의 기본값보다 우선합니다.

## `K_*_DEFINE` 매크로와 사용 함수

`K_*_DEFINE(...)`처럼 대문자로 된 매크로는 보통 전역 kernel object를 정적으로 만들어줍니다.
반대로 `k_*()`처럼 소문자로 시작하는 것은 실행 중에 호출하는 함수입니다.

### 현재 예제에서 쓰는 `DEFINE`

| 매크로 | 만들어지는 것 | 주 사용 함수 |
| --- | --- | --- |
| `K_THREAD_STACK_DEFINE(...)` | thread stack 메모리 | `k_thread_create()` |
| `K_SEM_DEFINE(...)` | `struct k_sem` | `k_sem_take()`, `k_sem_give()` |
| `K_MSGQ_DEFINE(...)` | `struct k_msgq`와 buffer | `k_msgq_put()`, `k_msgq_get()` |
| `K_MUTEX_DEFINE(...)` | `struct k_mutex` | `k_mutex_lock()`, `k_mutex_unlock()` |

현재 코드의 실제 이름은 아래처럼 연결됩니다.

| 코드 | 실제 의미 |
| --- | --- |
| `K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE)` | `thread_a_stack`이라는 stack 메모리 생성 |
| `K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE)` | `thread_b_stack`이라는 stack 메모리 생성 |
| `K_SEM_DEFINE(signal_sem, 0, 1)` | count 0, limit 1인 semaphore 생성 |
| `K_MSGQ_DEFINE(sample_msgq, sizeof(struct sample_msg), 8, 4)` | `sample_msg` 8개를 담는 message queue 생성 |
| `K_MUTEX_DEFINE(shared_counter_mutex)` | 공유 변수 보호용 mutex 생성 |

### 직접 선언한 뒤 init하는 객체

`K_*_DEFINE(...)`를 쓰지 않고 `static struct ...`로 직접 선언한 뒤 `k_*_init(...)`으로 초기화할 수도 있습니다.
현재 예제의 work와 timer는 이 방식을 씁니다.

| 직접 선언 | 초기화 | 사용 |
| --- | --- | --- |
| `struct k_work` | `k_work_init()` | `k_work_submit()` |
| `struct k_work_delayable` | `k_work_init_delayable()` | `k_work_schedule()`, `k_work_reschedule()` |
| `struct k_timer` | `k_timer_init()` | `k_timer_start()`, `k_timer_stop()` |

자주 쓰는 객체들은 대부분 아래처럼 두 가지 방식이 있습니다.

| 정적 DEFINE 방식 | 직접 선언 + init 방식 | 주 사용 함수 |
| --- | --- | --- |
| `K_TIMER_DEFINE(...)` | `struct k_timer` + `k_timer_init()` | `k_timer_start()`, `k_timer_stop()` |
| `K_WORK_DEFINE(...)` | `struct k_work` + `k_work_init()` | `k_work_submit()` |
| `K_WORK_DELAYABLE_DEFINE(...)` | `struct k_work_delayable` + `k_work_init_delayable()` | `k_work_schedule()`, `k_work_reschedule()` |
| `K_QUEUE_DEFINE(...)` | `struct k_queue` + `k_queue_init()` | `k_queue_append()`, `k_queue_get()` |
| `K_EVENT_DEFINE(...)` | `struct k_event` + `k_event_init()` | `k_event_post()`, `k_event_wait()` |
| `K_STACK_DEFINE(...)` | `struct k_stack` + `k_stack_init()` | `k_stack_push()`, `k_stack_pop()` |
| `K_MUTEX_DEFINE(...)` | `struct k_mutex` + `k_mutex_init()` | `k_mutex_lock()`, `k_mutex_unlock()` |
| `K_CONDVAR_DEFINE(...)` | `struct k_condvar` + `k_condvar_init()` | `k_condvar_wait()`, `k_condvar_signal()` |
| `K_SEM_DEFINE(...)` | `struct k_sem` + `k_sem_init()` | `k_sem_take()`, `k_sem_give()` |
| `K_MSGQ_DEFINE(...)` | `struct k_msgq` + `k_msgq_init()` | `k_msgq_put()`, `k_msgq_get()` |
| `K_MBOX_DEFINE(...)` | `struct k_mbox` + `k_mbox_init()` | `k_mbox_put()`, `k_mbox_get()` |
| `K_PIPE_DEFINE(...)` | `struct k_pipe` + `k_pipe_init()` | `k_pipe_write()`, `k_pipe_read()` |
| `K_MEM_SLAB_DEFINE(...)` | `struct k_mem_slab` + `k_mem_slab_init()` | `k_mem_slab_alloc()`, `k_mem_slab_free()` |
| `K_HEAP_DEFINE(...)` | `struct k_heap` + `k_heap_init()` | `k_heap_alloc()`, `k_heap_free()` |

추가로 init 함수는 있지만 보통 paired `K_*_DEFINE` 없이 쓰는 객체도 있습니다.

| 직접 선언 + init 방식 | 주 사용 함수 | 용도 |
| --- | --- | --- |
| `struct k_work_q` + `k_work_queue_init()` | `k_work_queue_start()` | 별도 workqueue thread 만들기 |
| `struct k_work_poll` + `k_work_poll_init()` | `k_work_poll_submit()` | poll event 기반 work |
| `struct k_poll_signal` + `k_poll_signal_init()` | `k_poll_signal_raise()` | `k_poll()`에 신호 전달 |
| `struct k_poll_event` + `k_poll_event_init()` | `k_poll()` | 여러 이벤트 중 하나 기다리기 |

### 예제에는 없지만 자주 쓰는 `DEFINE`

| 매크로 | 만들어지는 것 | 주 사용 함수 |
| --- | --- | --- |
| `K_THREAD_DEFINE(...)` | thread + stack | 부팅 때 자동 시작 |
| `K_TIMER_DEFINE(...)` | `struct k_timer` | `k_timer_start()`, `k_timer_stop()` |
| `K_WORK_DEFINE(...)` | `struct k_work` | `k_work_submit()` |
| `K_WORK_DELAYABLE_DEFINE(...)` | `struct k_work_delayable` | `k_work_schedule()`, `k_work_reschedule()` |
| `K_QUEUE_DEFINE(...)` | `struct k_queue` | `k_queue_append()`, `k_queue_get()` |
| `K_FIFO_DEFINE(...)` | `struct k_fifo` | `k_fifo_put()`, `k_fifo_get()` |
| `K_LIFO_DEFINE(...)` | `struct k_lifo` | `k_lifo_put()`, `k_lifo_get()` |
| `K_STACK_DEFINE(...)` | `struct k_stack` | `k_stack_push()`, `k_stack_pop()` |
| `K_EVENT_DEFINE(...)` | `struct k_event` | `k_event_post()`, `k_event_wait()` |
| `K_CONDVAR_DEFINE(...)` | `struct k_condvar` | `k_condvar_wait()`, `k_condvar_signal()` |
| `K_MEM_SLAB_DEFINE(...)` | `struct k_mem_slab` | `k_mem_slab_alloc()`, `k_mem_slab_free()` |
| `K_HEAP_DEFINE(...)` | `struct k_heap` | `k_heap_alloc()`, `k_heap_free()` |
| `K_PIPE_DEFINE(...)` | `struct k_pipe` | `k_pipe_write()`, `k_pipe_read()` |
| `K_MBOX_DEFINE(...)` | `struct k_mbox` | `k_mbox_put()`, `k_mbox_get()` |

구분하는 기준:

- `K_*_DEFINE(...)`: 컴파일 타임에 객체를 만들어주는 매크로입니다.
- `static struct ...`: C 문법으로 직접 객체를 선언합니다.
- `k_*_init(...)`: 이미 있는 객체를 실행 중 초기화합니다.
- `k_*_take()`, `k_*_give()`, `k_*_put()`, `k_*_get()`, `k_*_submit()`: 초기화된 객체를 실제로 사용합니다.

## 1. Thread

먼저 실습 번호를 `1`로 설정합니다.

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
