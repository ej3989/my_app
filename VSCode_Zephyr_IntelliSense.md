# VS Code Zephyr IntelliSense 설정

이 문서는 Zephyr 애플리케이션을 VS Code에서 볼 때 C/C++ IntelliSense가 실제 빌드 설정을 따라가도록 구성하는 방법을 정리한다.

## 핵심 설정

워크스페이스 루트가 `/Volumes/ej_disk/zephyrproject`인 경우 `.vscode/settings.json`은 아래처럼 최소 구성으로 둘 수 있다.

```json
{
  "C_Cpp.default.compileCommands": "${workspaceFolder}/EJ_APP/build/compile_commands.json",
  "C_Cpp.files.exclude": {
    "**/.git": true,
    "**/build/CMakeFiles/**": true,
    "**/build/zephyr/zephyr_pre*.map": true,
    "**/zephyr/tests/**": true,
    "**/zephyr/samples/**": true
  },
  "C_Cpp.errorSquiggles": "disabled"
}
```

## compileCommands

```json
"C_Cpp.default.compileCommands": "${workspaceFolder}/EJ_APP/build/compile_commands.json"
```

이 설정이 가장 중요하다. `west build`를 실행하면 CMake가 실제 컴파일 명령을 만들고, 그 결과가 `compile_commands.json`에 저장된다.

이 파일 안에는 단순 include path뿐 아니라 Zephyr 빌드에 필요한 옵션들이 같이 들어간다.

```text
-I...
-D...
-imacros .../autoconf.h
-include .../zephyr_compat.h
--sysroot=...
-std=c17
```

따라서 VS Code C/C++ 확장은 직접 작성한 `includePath`보다 `compile_commands.json`을 기준으로 해석하는 것이 실제 빌드와 더 잘 맞는다.

## includePath를 직접 많이 넣지 않는 이유

Zephyr는 일반 C 프로젝트보다 빌드 설정이 복잡하다.

예를 들어 `CONFIG_...` 값은 generated header인 `autoconf.h`에서 오고, devicetree 관련 매크로도 빌드 중 생성된 헤더를 사용한다. LVGL 같은 모듈도 Zephyr 설정을 통해 include path와 define이 함께 결정된다.

그래서 `.vscode/settings.json`에 아래와 같은 수동 include path를 많이 넣는 방식은 fallback으로는 쓸 수 있지만, 실제 빌드 설정을 완전히 대체하기 어렵다.

```json
"C_Cpp.default.includePath": [
  "...",
  "..."
]
```

정상적인 흐름은 다음과 같다.

```text
west build
  -> CMake가 실제 gcc 명령 생성
  -> compile_commands.json 생성
  -> VS Code C/C++ 확장이 compile_commands.json 읽음
  -> main.c에 필요한 -I, -D, -imacros 옵션 적용
```

## build 디렉토리 통일

이 설정은 아래 파일을 바라본다.

```text
EJ_APP/build/compile_commands.json
```

따라서 앱을 바꿔도 계속 `EJ_APP/build`를 사용하려면 `west build`에서 `-d build`를 명시하는 것이 좋다.

예:

```sh
cd /Volumes/ej_disk/zephyrproject/EJ_APP

west build -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  lvgl_practice \
  -d build \
  -S espressif-flash-16M \
  -S espressif-psram-8M
```

다른 앱으로 바꿀 때도 같은 build 디렉토리를 재사용한다.

```sh
cd /Volumes/ej_disk/zephyrproject/EJ_APP

west build -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  new_app \
  -d build \
  -S espressif-flash-16M \
  -S espressif-psram-8M
```

이렇게 하면 VS Code 설정은 그대로 두고 `EJ_APP/build/compile_commands.json`만 새 앱 기준으로 갱신된다.

## files.exclude

```json
"C_Cpp.files.exclude": {
  "**/.git": true,
  "**/build/CMakeFiles/**": true,
  "**/build/zephyr/zephyr_pre*.map": true,
  "**/zephyr/tests/**": true,
  "**/zephyr/samples/**": true
}
```

이 설정은 VS Code C/C++ 확장이 너무 많은 파일을 탐색하지 않도록 일부 경로를 제외한다.

Zephyr repository에는 테스트, 샘플, 빌드 산출물이 많기 때문에 필요 없는 경로를 제외하면 탐색 속도와 인덱싱 부담을 줄일 수 있다.

## errorSquiggles

```json
"C_Cpp.errorSquiggles": "disabled"
```

Zephyr는 매크로, generated header, devicetree 코드 생성이 많아서 IDE가 실제 빌드는 문제없는 코드를 오류처럼 표시하는 경우가 있다.

이 설정은 빨간 물결 표시를 끈다. 코드 이동, 심볼 검색, 자동완성은 계속 사용할 수 있다.

물결 표시까지 보고 싶다면 아래처럼 바꿀 수 있다.

```json
"C_Cpp.errorSquiggles": "enabled"
```

다만 Zephyr 프로젝트에서는 오진이 있을 수 있다.

## 설정을 바꾼 뒤 할 일

설정을 바꾼 뒤 VS Code에서 아래 명령을 실행한다.

```text
Cmd + Shift + P
Developer: Reload Window
```

그래도 include 오류가 남으면 C/C++ 확장의 IntelliSense 데이터베이스를 다시 만든다.

```text
C/C++: Reset IntelliSense Database
C/C++: Restart IntelliSense Engine
```

## 문제 확인 방법

현재 열린 소스 파일이 `compile_commands.json`에 들어 있는지 확인한다.

예:

```sh
rg 'lvgl_practice/src/main.c' EJ_APP/build/compile_commands.json
```

결과가 나오면 VS Code가 봐야 할 compile database에는 해당 파일이 들어 있는 것이다.

만약 결과가 없으면 현재 build 디렉토리가 다른 앱 기준으로 만들어진 상태다. 이 경우 `west build -p always ... -d build`로 다시 구성한다.
