# VS Code Zephyr Debug with cppdbg

This workspace uses VS Code's Microsoft C/C++ debugger (`cppdbg`) to attach to
an ESP32-S3 Zephyr target through OpenOCD.

## Required Extension

Install the Microsoft C/C++ extension:

```text
ms-vscode.cpptools
```

The workspace recommends this extension in `.vscode/extensions.json`.

## Debug Flow

Start the OpenOCD GDB server first:

```sh
west debugserver
```

Then in VS Code:

```text
Run and Debug
-> Attach to Zephyr ESP32-S3 OpenOCD (cppdbg)
-> Start Debugging
```

The connection chain is:

```text
VS Code cppdbg
  -> xtensa-espressif_esp32s3_zephyr-elf-gdb
    -> localhost:3333
      -> OpenOCD
        -> ESP32-S3
```

## launch.json

Current `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Attach to Zephyr ESP32-S3 OpenOCD (cppdbg)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/EJ_APP/build/zephyr/zephyr.elf",
      "cwd": "${workspaceFolder}",
      "MIMode": "gdb",
      "miDebuggerPath": "/Users/jaeheelee/zephyr-sdk-1.0.1/gnu/xtensa-espressif_esp32s3_zephyr-elf/bin/xtensa-espressif_esp32s3_zephyr-elf-gdb",
      "miDebuggerServerAddress": "localhost:3333",
      "externalConsole": false,
      "stopAtEntry": false,
      "setupCommands": [
        {
          "text": "set remotetimeout 10"
        },
        {
          "text": "set confirm off"
        },
        {
          "text": "set print pretty on"
        }
      ]
    }
  ]
}
```

## Field Meaning

```json
"type": "cppdbg"
```

Uses the Microsoft C/C++ debugger integration.

```json
"request": "launch"
```

For `cppdbg`, this can still be used with `miDebuggerServerAddress` to connect
to an already running GDB server.

```json
"program": "${workspaceFolder}/EJ_APP/build/zephyr/zephyr.elf"
```

Loads Zephyr debug symbols from the built ELF file.

```json
"MIMode": "gdb"
```

Tells `cppdbg` to use GDB/MI.

```json
"miDebuggerPath": ".../xtensa-espressif_esp32s3_zephyr-elf-gdb"
```

Uses the ESP32-S3 Xtensa GDB from the Zephyr SDK.

```json
"miDebuggerServerAddress": "localhost:3333"
```

Attaches to the OpenOCD GDB server. `west debugserver` opens this port.

## Useful Debug Console Commands

```gdb
c
n
s
finish
bt
p variable_name
p/x address_or_value
```

Meaning:

```text
c       continue
n       step over
s       step into
finish  step out
bt      backtrace
p       print expression
p/x     print expression in hex
```

## Notes

- Start `west debugserver` before launching VS Code debug.
- Keep the ELF path in `program` matched to the active build directory.
- If step buttons behave oddly, use the Debug Console commands `n`, `s`, and
  `finish` directly.
- For readable stepping, build with debug-friendly options such as
  `CONFIG_DEBUG_OPTIMIZATIONS=y`.
