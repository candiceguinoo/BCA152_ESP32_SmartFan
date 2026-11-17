# Voice-Controlled Smart Fan (ESP32 + Firebase)

## Project Overview

This project is a three-speed fan controller using an ESP32. It allows the user to control the fan using four physical buttons or through a web page.

The web page provides buttons for selecting the fan speed and also supports voice commands through the browser. Firebase Realtime Database is used to keep the ESP32 and the web page updated with the current fan level.

This project is based on Chapter 12 of *Internet of Things with ESP32* by Packt Publishing. The original project uses AWS IoT Core, Lambda, and Alexa. However, I used Firebase and browser-based voice recognition as alternatives because the original cloud setup required payment-related services.

The following table shows the main differences between the original implementation and our project.

| Original Book Implementation | My Implementation |
|---|---|
| ESP-IDF framework | Arduino framework using PlatformIO |
| AWS IoT Core | Firebase Realtime Database |
| AWS Lambda and Alexa Skill | Web page with browser voice recognition |
| Logic converter and three relays | Relay modules connected directly to ESP32 GPIO pins |

## Features

- Four physical buttons: Off, Low (33%), Normal (66%), High (100%).
- Web page with speed buttons, a spinning fan icon that shows the current level, and a status message.
- Voice commands in the browser: "set fan to 50", "set to high", "low", "increase by 10", "turn off".
- Two-way sync: a button press updates the web page, and a web or voice command switches the relays.
- Wi-Fi scan and disconnect-reason output in the serial monitor for troubleshooting.
- Credentials kept out of Git (`secrets.h` is ignored, `secrets.h.example` is the template).

## Learning Objectives

- Read buttons on GPIO pins with internal pull-ups and software debounce.
- Drive relay modules from GPIO pins, including active-low behavior.
- Connect an ESP32 to Wi-Fi and sign in to a cloud service.
- Keep a device and a web client in sync with a desired/reported data model.
- Build a small web client with Firebase Authentication, Realtime Database and the Web Speech API.
- Debug hardware and network problems step by step, and record the process in Git history and `DEVLOG.md`.

## System Architecture
(pending)


## Hardware / Simulated Components

All parts are real hardware. Nothing is simulated.

| Component | Quantity | Description |
|---|:---:|---|
| ESP32 WROOM-32 (AZ-Delivery DevKit V4) | 1 | Main controller |
| Push buttons | 4 | Used to select the fan speed |
| 2-channel relay module | 1 | Produce clicking sounds and indicate fan level switching |
| 1-channel relay module | 1 | Produce clicking sounds and indicate fan level switching |
| Breadboard | 2 | Used for circuit connections |
| Jumper wires | As needed | Used to connect the components |

NOTE: No mains voltage or real fan was connected during testing. The relay outputs were tested for switching only.

## Pin Configuration

| ESP32 pin | Connects to | Purpose |
|---|---|---|
| GPIO19 | Button 1, other leg to GND | Off |
| GPIO18 | Button 2, other leg to GND | Low (33%) |
| GPIO5 | Button 3, other leg to GND | Normal (66%) |
| GPIO21 | Button 4, other leg to GND | High (100%) |
| GPIO32 | 2-channel relay IN1 | Relay for Low |
| GPIO33 | 2-channel relay IN2 | Relay for Normal |
| GPIO25 | 1-channel relay IN | Relay for High |
| VIN | VCC of both relay modules | Power supply |
| GND | Common GND rail | Relay modules and buttons |

NOTE: Both relay modules get their power through the ESP32 VIN pin. Buttons use the ESP32's internal pull-ups, so no resistors are needed. The relay modules are active-low: a relay switches on when its input is pulled LOW, and is switched off by letting the pin float.

## Task Design

There are no separate tasks. The work is split into functions that `loop()` calls in order:

| Function | Job |
|---|---|
| `handleButtons()` | Reads the four buttons with a 150 ms debounce and applies a level on a press |
| `pollDesired()` | Reads `/fan/desired` once a second and applies it if it changed |
| `applyLevel()` | Turns all relays off, waits 50 ms, then turns the right one on |
| `publishLevel()` | Writes `desired` and `reported` to Firebase |
| `connectWiFi()` | Scans networks, prints diagnostics and connects (restarts after 20 s) |

NOTE: Buttons are handled before the Firebase check, so they work even while the internet is down.


## Task Design

There are no separate tasks. The work is split into functions that `loop()` calls in order:

| Function | Job |
|---|---|
| `handleButtons()` | Reads the four buttons with a 150 ms debounce and applies a level on a press |
| `pollDesired()` | Reads `/fan/desired` once a second and applies it if it changed |
| `applyLevel()` | Turns all relays off, waits 50 ms, then turns the right one on |
| `publishLevel()` | Writes `desired` and `reported` to Firebase |
| `connectWiFi()` | Scans networks, prints diagnostics and connects (restarts after 20 s) |

Buttons are handled before the Firebase check, so they work even while the internet is down.

## Inter-Task Communication
(pending)


## State Machine

The fan has four states. Any button press, web click or voice command moves it to the requested level.

(pending)


## Repository Structure

(pending)


## Getting Started

You need VS Code with the PlatformIO extension, a Google account for Firebase, and a 2.4 GHz Wi-Fi network (the ESP32 cannot use 5 GHz).

1. **Firebase:** create a project, add a Realtime Database, enable Email/Password sign-in, and add two users (one for the ESP32, one for the web page). Register a web app to get the web API key.
2. **Database rules:** paste these in Realtime Database > Rules:

   ```json
   {
     "rules": {
       "fan": {
         ".read": "auth != null",
         ".write": "auth != null"
       }
     }
   }
   ```

3. **Secrets:** copy `include/secrets.h.example` to `include/secrets.h` and fill in the Wi-Fi name and password, the web API key, and the ESP32 user's email and password.
4. **Web page:** in `web/index.html`, replace `PASTE_YOUR_API_KEY` with the same web API key.
5. **Wire the circuit** as in the Pin Configuration table, with the ESP32 unplugged.


## Building the Project

From the `smart_fan` folder in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor
```

Press the EN button on the board after the monitor opens. You should see `Connected, IP: ...`, then `status = ready`, then `Firebase ready. Waiting for buttons or web commands.`

Run the web page (voice recognition needs `localhost` or https):

```powershell
cd web
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m http.server 8000
```

Open `http://localhost:8000` in Chrome or Edge and sign in with the web user.

NOTE: Two switches in `main.cpp` help with testing: `RELAY_SELF_TEST` cycles the relays once at boot, and `DEBUG_BUTTONS` prints a line each time a button changes state. Both are set to 0 for normal use.

## Photos and Video of the Working Circuit

This section replaces the Wokwi simulation, because the project was built and tested on real hardware. 

(pending)

## Unit Testing
(pending)

## Functional Verification

| Test | Expected | Result |
|---|---|---|
| Power-up, nothing pressed | No relay light on, four buttons read "released" | Pass |
| Press Off, Low, Normal, High buttons | One `Fan level` line per press, matching relay lights, Firebase `reported` updates | Pass |
| Relay lights | Off: none. Low: Module A relay 1. Normal: Module A relay 2. High: Module B relay 1 only | Pass |
| Relays make no buzzing sound | No buzzing | Pass |
| Web page buttons | Relays switch within about 1 to 2 seconds | Pass |
| Press a physical button | Web page level and fan icon update | Pass |
| Voice: "set fan to 50" | Normal (66), Module A relay 2 on | Pass |
| Voice: "high", "increase by 10", "turn off" | High, +10 then rounded, Off | Pass |

(pending)

## Engineering Decisions

(pending)

## Limitations

- Alexa is not used. Voice control works only in the browser (Chrome or Edge).
(pending)

## Future Improvements
(pending)

## References and Acknowledgments

- *Internet of Things with ESP32*, Packt Publishing, Chapter 12 (code: github.com/PacktPublishing/Internet-of-Things-with-ESP32).
- Firebase documentation: Realtime Database and Authentication.
- Firebase Arduino Client Library for ESP8266 and ESP32 (mobizt).
- MDN Web Docs: Web Speech API (speech recognition).
- An AI assistant (Claude, by Anthropic) was used for help with debugging and drafting this README. All hardware was wired, tested and verified by the student.

