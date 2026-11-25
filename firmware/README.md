# ESP32 + SIM800L tracker

Based on the supplied GPS/OLED firmware. Original wiring is unchanged:
GPS RX/TX 32/33, SIM800L RX/TX 14/13, OLED SDA/SCL 21/22.

Copy `include/tracker_config.example.h` to `include/tracker_config.h` and fill in
your SIM provider's APN and the same `TRACKER_API_KEY` configured in Railway.
Use a unique device ID containing letters, numbers or hyphens. Keep configuration
private; `tracker_config.h` is ignored by Git.

Run `pio run` to compile, then `pio run -t upload` with the ESP32 connected.
Serial monitor speed is 115200. The uploader sends a heartbeat even without a GPS fix. Verified HTTPS needs
UTC time from GPS or the cellular network, plus cellular registration and GPRS. It submits a heartbeat or latest fix every
15 seconds when connectivity permits; failed requests are replaced by the next
fresh fix, with no offline backlog. Successful storage prints
`[UPLOAD] Position saved to dashboard.`

The modem UART is now owned by TinyGSM: manual serial AT passthrough is removed.
Networking runs in a separate task while GPS parsing and the OLED keep updating.
HTTPS certificate verification uses official ISRG roots and GPS UTC time; it
does not disable certificate checks. Keep roots updated if the host changes its
certificate issuer. TLS uses ESP_SSLClient over TinyGSM TCP, rather than the
SIM800L's older built-in SSL stack.

Open https://tracker-production-87a1f764.up.railway.app/ and enter the API key to
see received locations. No hardware flash or physical cellular test has been
performed as part of source preparation. The SIM needs working 2G/GPRS service.

The dashboard marks devices online for 90 seconds after the last received message.
Heartbeat-only messages update device status without adding fake map positions.

Cellular TLS uses an 8 KiB UART receive buffer, a 90-second handshake timeout,
and a 60-second response wait. The upload task runs at idle priority so
FreeRTOS idle tasks can service the watchdog during software TLS operations.
Certificate and hostname verification remain enabled.
