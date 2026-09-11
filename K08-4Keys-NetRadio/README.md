# K08 NetRadio

ESP32-S3 internet radio example for the Amour K08 board.

The project board definition targets an ESP32-S3-N16R8 module with 16 MB Flash
and 8 MB octal PSRAM. It uses QIO Flash, OPI PSRAM and the Arduino
`default_16MB.csv` dual-OTA partition layout.

The USB CDC serial console is enabled at boot. At 115200 baud, startup logs
report the detected Flash and PSRAM sizes plus AHT20 initialization status.

## Build

From the repository root:

```sh
pio run -d K08-4Keys-NetRadio
```

Upload through USB:

```sh
pio run -d K08-4Keys-NetRadio -t upload
```

## OTA configuration

OTA is disabled unless a password is configured. Copy the example file and set
a strong device-local password:

```sh
cp K08-4Keys-NetRadio/include/secrets.example.h \
   K08-4Keys-NetRadio/include/secrets.h
```

`include/secrets.h` is ignored by Git. Do not commit real credentials.

After the first USB upload, configure `upload_protocol = espota`, `upload_port`
and an `upload_flags = --auth=...` value matching the device password. Keep the
real authentication value outside version control.

## Controls

- Add key: next station; double-click to increase volume.
- Minus key: previous station; double-click to decrease volume.
- Mode key: pause or resume playback.

## Runtime recovery

- Stored Wi-Fi credentials are tried for 10 seconds.
- ESPTouch SmartConfig is available for up to 2 minutes when connection fails.
- A disconnected Wi-Fi station is retried every 5 seconds.
- A failed stream is retried three times before the next station is selected.
- NTP failure no longer prevents the radio from starting.
