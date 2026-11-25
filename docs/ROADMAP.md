# Specter roadmap

Future candidates; these are not claims of implemented functionality.

## Next priorities
- Finish BLE touchscreen live results, Stop/Back, retained results and clean menu restoration.
- Capture inspector: saved PCAP metadata, AP/client summaries, handshake-message completeness; evaluate hc22000 export.
- Deeper IoT inventory: mDNS/SSDP discovery, service names and evidence-backed device identification.
- Rogue AP watch and Session comparison: trusted baselines, new/missing devices and security-setting changes.

## Further additions
- USB capture streaming compatible with Wireshark/extcap.
- WiFi security audit: encryption, advertised PMF/WPS information and configuration changes.
- Dual-band channel survey with signal, observed traffic and history.
- BLE advertisement and GATT service/characteristic inspector.
- Tracker sighting history across Sessions, with classification confidence.
- Session report export alongside original evidence files.
- Structured Guardian alerts linked to Sessions and available GPS/time metadata.
- Verified firmware updates and recovery/rollback, subject to partition planning.

## Hardware-dependent research
Sub-GHz, NFC, IR and nRF24 support require verified radio hardware, wiring and resource budgets.

Reference projects: [Bruce](https://github.com/BruceDevices/firmware),
[GhostESP](https://github.com/GhostESP-Revival/GhostESP),
[Evil-M5Project](https://github.com/7h30th3r0n3/Evil-M5Project).
Review implementation, licensing and ESP32-C5 compatibility before adopting code.
