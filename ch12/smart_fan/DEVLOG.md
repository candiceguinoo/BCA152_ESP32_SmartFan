# Development Log

Each entry records what I tried, what happened, and what I changed.

## Session 1: Planning and AWS attempt
- Followed book chapter 12 (ESP-IDF, AWS IoT Core, Lambda, Alexa).
- AWS needs a payment card, which I don't have, so I switched to Firebase (free plan).
- Trade-off: Alexa needs paid Cloud Functions, so voice control uses the browser.

## Session 2: Firebase and firmware
- Created the Firebase project, Realtime Database and email/password users.
- Rewrote the firmware for the Arduino framework with the Firebase client library.
- Removed the old ESP-IDF and AWS files. secrets.h is ignored by Git.

## Session 3: Wi-Fi problems
- "Set status to INIT" errors: the home network was 5 GHz only, and the ESP32 needs 2.4 GHz.
- Added a network scan and disconnect-reason output. Reason 5 meant the hotspot's device limit was full.
- Switched to a 2.4 GHz phone hotspot.

## Session 4: Buttons
- Moved the 100% button from GPIO17 to GPIO21.
- Raised the debounce from 50 ms to 150 ms to stop double presses.
- Added button debug output. Pins 5 and 21 read as permanently pressed: a wiring short.

## Session 5: Relays
- Wired two 2-channel relay modules. Module B did not respond and one relay buzzed.
- Removed the logic converter and drove the relays directly from the GPIO pins.
- Both Module A lights came on at Off and High: the modules are active-low.
- Fix: per-relay polarity, with the pin floating for "off".

## Session 6: Web page
- The fan icon blades swung off-center and out of view. Cause: CSS transform-origin
  percentages were measured from the wrong point in the SVG.
- Fix: SVG animateTransform with an explicit pivot at the hub.