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
