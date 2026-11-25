# ESP32 + SIM800L tracker

Based on the supplied GPS/OLED firmware. Original wiring is unchanged:
GPS RX/TX 32/33, SIM800L RX/TX 14/13, OLED SDA/SCL 21/22.

Copy `include/tracker_config.example.h` to `include/tracker_config.h` and fill in
your SIM provider's APN and the same `TRACKER_API_KEY` configured in Railway.
Use a unique device ID containing letters, numbers or hyphens. Keep configuration
private; `tracker_config.h` is ignored by Git.

Run `pio run` to compile, then `pio run -t upload` with the ESP32 connected.
Serial monitor speed is 115200. The uploader requires fresh GPS coordinates and
UTC date/time, cellular registration and GPRS. It submits the latest fix every
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
