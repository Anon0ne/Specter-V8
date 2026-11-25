# Third-party notices and source provenance

This is a Marauder V8 / ESP32-C5 source snapshot maintained by Anon0ne, derived
from ESP32 Marauder. The original upstream MIT license and source copyright
notices are preserved. Components keep their own licenses; root MIT licensing
does not replace the terms of bundled or linked third-party components.

The pinned library versions and source revisions are in
`tools/v8_dependencies.json`. Their collected license texts and source URLs are
in `docs/third-party/`. Arduino-ESP32 3.3.4 and its ESP-IDF/toolchain dependencies
are downloaded by the build helper; their sources and notices remain available
from the pinned Espressif distribution. The public source includes the build
configuration needed to rebuild this V8 image.

The additional firmware modules were supplied and maintained in the project's
private development line. Existing module comments and attribution are retained.
Historical assertions about clean-room derivation have not been independently
verified by this release audit; no new provenance certification is made here.
The owner authorized publication of this snapshot. Report any missing source
attribution or licensing concern privately using `SECURITY.md`.

V8 TFT/touch configuration and C5 support use the H4W9/TFT_eSPI source revision
pinned in the dependency manifest. This is not the proprietary official V8
library distribution and does not imply endorsement by the upstream authors.

No private Pro plugin, license server, signing key, test capture or legacy PCB
cache is included. Heatmap is separately available through premium membership
purchased directly from Anon0ne and is not part of this public beta.
