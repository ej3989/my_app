# Zephyr Wi-Fi Credentials Build Guide

## 목적

Wi-Fi SSID와 비밀번호를 Git 저장소에 올리지 않으면서 빌드 시점에
Zephyr 애플리케이션 설정에 포함하는 방법을 정리한다.

이 프로젝트에서는 다음 파일을 사용한다.

| 파일 | 역할 | Git 포함 여부 |
|---|---|---|
| `lvgl_practice/Kconfig` | Wi-Fi 설정 항목과 기본값 정의 | 포함 |
| `lvgl_practice/prj.conf` | 공통 애플리케이션 설정 | 포함 |
| `lvgl_practice/wifi_credentials.conf.example` | 자격 증명 작성 예제 | 포함 |
| `lvgl_practice/wifi_credentials.conf` | 실제 SSID와 비밀번호 | 제외 |

## 1. Kconfig 정의

`lvgl_practice/Kconfig`에는 설정의 형식만 정의하고 실제 값은 넣지 않는다.

```kconfig
config APP_WIFI_SSID
	string "Wi-Fi SSID"
	default ""

config APP_WIFI_PSK
	string "Wi-Fi password"
	default ""
```

이 설정은 C 코드에서 다음 매크로로 사용된다.

```c
CONFIG_APP_WIFI_SSID
CONFIG_APP_WIFI_PSK
```

## 2. 로컬 자격 증명 파일

처음 구성할 때 예제 파일을 복사한다.

```sh
cd /Volumes/ej_disk/zephyrproject/EJ_APP
cp lvgl_practice/wifi_credentials.conf.example \
   lvgl_practice/wifi_credentials.conf
```

`lvgl_practice/wifi_credentials.conf`에 실제 값을 입력한다.

```ini
CONFIG_APP_WIFI_SSID="your-ssid"
CONFIG_APP_WIFI_PSK="your-password"
```

이 파일은 `.gitignore`의 다음 규칙으로 제외된다.

```gitignore
wifi_credentials.conf
```

ignore 적용 여부는 다음 명령으로 확인한다.

```sh
git check-ignore -v lvgl_practice/wifi_credentials.conf
```

## 3. 최초 빌드 명령

`EXTRA_CONF_FILE`을 이용해 비공개 설정 파일을 추가한다.

```sh
west build -p always -d build \
  -b esp32s3_devkitc/esp32s3/procpu \
  lvgl_practice \
  -S espressif-flash-16M \
  -S espressif-psram-8M \
  -- -DEXTRA_CONF_FILE=wifi_credentials.conf
```

명령의 주요 부분은 다음과 같다.

- `--`: 이후 인수를 CMake에 전달한다.
- `-D`: CMake 캐시 변수를 설정한다.
- `EXTRA_CONF_FILE`: 기본 설정에 추가로 병합할 Kconfig fragment를 지정한다.
- `wifi_credentials.conf`: 애플리케이션 폴더인 `lvgl_practice` 기준 경로다.

상대 경로가 정상적으로 처리되지 않으면 절대 경로를 사용할 수 있다.

```sh
-- -DEXTRA_CONF_FILE=/Volumes/ej_disk/zephyrproject/EJ_APP/lvgl_practice/wifi_credentials.conf
```

## 4. 설정이 연결되는 과정

```text
lvgl_practice/Kconfig
  설정 항목과 기본값 정의
           |
           v
lvgl_practice/prj.conf
  공통 프로젝트 설정
           |
           v
lvgl_practice/wifi_credentials.conf
  실제 SSID와 비밀번호 추가
           |
           v
build/zephyr/.config
  최종 Kconfig 결과
           |
           v
build/zephyr/include/generated/zephyr/autoconf.h
  CONFIG_APP_WIFI_* C 매크로 생성
           |
           v
src/wifi_service.c
  Wi-Fi 연결 요청에 매크로 사용
```

생성된 C 매크로는 개념적으로 다음과 같다.

```c
#define CONFIG_APP_WIFI_SSID "your-ssid"
#define CONFIG_APP_WIFI_PSK  "your-password"
```

`wifi_service.c`에서는 일반 문자열처럼 사용한다.

```c
params.ssid = (const uint8_t *)CONFIG_APP_WIFI_SSID;
params.psk = (const uint8_t *)CONFIG_APP_WIFI_PSK;
```

## 5. 이후 빌드

최초 구성 후 `EXTRA_CONF_FILE` 경로는 `build/CMakeCache.txt`에 저장된다.
같은 빌드 디렉터리를 계속 사용한다면 일반적으로 다음 명령만 실행하면 된다.

```sh
west build -d build
```

다음 경우에는 최초 빌드 명령처럼 `EXTRA_CONF_FILE`을 다시 지정한다.

- 새로운 빌드 디렉터리를 만들었을 때
- 기존 빌드 디렉터리를 삭제했을 때
- pristine build로 CMake 캐시를 다시 구성할 때
- 다른 자격 증명 파일을 사용하려 할 때

자격 증명 값을 수정한 후 설정 반영이 확실해야 한다면 다음처럼 다시 구성한다.

```sh
west build -p always -d build \
  -b esp32s3_devkitc/esp32s3/procpu \
  lvgl_practice \
  -S espressif-flash-16M \
  -S espressif-psram-8M \
  -- -DEXTRA_CONF_FILE=wifi_credentials.conf
```

## 6. 결과 확인

최종 설정 파일에 항목이 생성되었는지 확인할 수 있다.

```sh
grep 'CONFIG_APP_WIFI_' build/zephyr/.config
```

이 명령은 터미널에 실제 비밀번호를 출력하므로 화면 공유나 로그 저장 중에는
사용하지 않는다.

## 7. 보안 주의사항

- `.gitignore`는 아직 추적되지 않은 파일만 새로 제외한다.
- 자격 증명을 이미 커밋했다면 파일에서 삭제해도 Git 과거 기록에는 남는다.
- 원격 저장소에 비밀번호를 push했다면 해당 Wi-Fi 비밀번호를 변경한다.
- 자격 증명은 최종 `zephyr.elf`와 펌웨어 이미지에 포함될 수 있다.
- 제품 수준 보안이 필요하면 런타임 프로비저닝과 보안 저장소 사용을 검토한다.
- `build/` 디렉터리도 실제 값을 포함하므로 외부에 배포하지 않는다.

## 요약

```text
공개 파일: Kconfig, prj.conf, wifi_credentials.conf.example
비공개 파일: wifi_credentials.conf
빌드 연결: -- -DEXTRA_CONF_FILE=wifi_credentials.conf
```
