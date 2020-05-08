# ESPParty
Little arduino projects for ESP8266 and ESP32. Mostly party lights.

## Projects
### espdmx
A simple arduino sketch to use WS2812 led strips with an ESP8266 or ESP32 via E1.31 DMX over Ethernet
### Relais
A simple arduino sketch to control solid state relais with an ESP via  E1.31 DMX over Ethernet.

## Dependencies
Almost the same everywhere, so I only list them once here
### ESPAsyncE131
Do not use the version from library manager, use the git master instead: https://github.com/forkineye/ESPAsyncE131
### ESPAsyncUDP (only ESP8266)
Not available via library manager, use git master: https://github.com/me-no-dev/ESPAsyncUDP
### NeoPixelBus
Install via library manager or git: https://github.com/Makuna/NeoPixelBus
### IotWebConf
Install via library manager or git: https://github.com/prampec/IotWebConf
