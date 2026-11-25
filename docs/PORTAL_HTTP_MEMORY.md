# V8 portal HTTP startup memory

The ESP32-C5 build can have enough total internal RAM but no contiguous block
large enough for AsyncTCP 3.4.8's default 16,384-byte task stack. On the test V8,
portal startup reported 28,136 bytes free but a largest block of 15,348 bytes;
port 80 failed to listen. The 8,192-byte task stack build reached the TCP LISTEN
state with the same largest block (15,348 bytes).

Both V8 build helpers apply `-DCONFIG_ASYNC_TCP_STACK_SIZE=8192` to C and C++
compilation, including the AsyncTCP library. Other build systems must apply the
same library-wide flag; defining it only in the sketch does not configure an
independently compiled library. The flag does not change Wi-Fi or BLE stacks.

For startup diagnostics:

```sh
SPECTER_HTTP_DIAGNOSTICS=1 bash tools/build_v8_ble.sh /tmp/specter-http-build
```

This enables framework error logging and prints internal free/largest-block RAM
before listener creation. No submitted form values are added to these diagnostics.

The portal registers its handlers once, rebuilds the TCP listener per activation,
and checks LISTEN before declaring ready. A failed listener emits
`@PORTAL:ERROR HTTP_LISTEN_FAILED`; startup cleanup also reports
`@PORTAL:ERROR AP_START_FAILED` and removes the SoftAP.

After Stop, the existing Wi-Fi lifecycle clears the AP selection; reconfigure
the SSID before restarting. Validate with a client connected to the test SSID
and an explicit HTTP GET to `http://172.0.0.1/`. TCP LISTEN is checked on the
board; browser delivery and stack headroom under sustained load are separate
acceptance checks. The exact first historical revision that exhausted the
allocation headroom has not been isolated.
