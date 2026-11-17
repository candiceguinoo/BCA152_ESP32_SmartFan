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



