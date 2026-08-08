# Pico 2 W CYW43439 resources

The firmware and CLM files in this directory are extracted from the combined
CYW43439 resource bundled with Raspberry Pi Pico SDK 2.2.0:

`lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h`

The matching NVRAM parameters are based on Pico SDK's
`lib/cyw43-driver/firmware/wifi_nvram_43439.h`.

To regenerate the binary resources, run:

```sh
python3 scripts/extract_pico_cyw43_resources.py \
  /path/to/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h \
  resources
```

Expected output sizes:

- `pico2w-cyw43439-firmware.bin`: 224190 bytes
- `pico2w-cyw43439.clm_blob`: 984 bytes
