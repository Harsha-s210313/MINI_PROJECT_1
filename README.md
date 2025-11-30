# MINI_PROJECT_1
We developed a cooperative swarm-based street light control system featuring vehicle interaction. Our project enables street lights to communicate dynamically, adjusting brightness based on real-time traffic to save energy while ensuring safety. This repository contains the source code and simulation data for our implementation.
# Cooperative Swarm-Based Street Light Control with Vehicle Interaction and Accident Detection

![Status](https://img.shields.io/badge/Status-Prototype%20V1.0-success)
![Team](https://img.shields.io/badge/Team-ELECTRO--SAPIENS-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32-green)
![License](https://img.shields.io/badge/License-MIT-orange)

## 📜 Abstract

This project presents a novel, decentralized approach to smart city infrastructure. Moving away from traditional cloud-dependent architectures, we propose a **Cooperative Swarm Intelligence** system where street lights act as autonomous nodes capable of Peer-to-Peer (P2P) communication.

The system features a **Dynamic Lighting Corridor** that tracks vehicles in real-time to provide predictive illumination, reducing energy consumption by up to 60%. Furthermore, it integrates a critical **Accident Detection & Response Mechanism**. Using sensor fusion (Vibration + GPS), the network validates collision events, triggers immediate visual strobe warnings for oncoming traffic, and routes distress signals via a **Central IoT Gateway** to emergency services.

## 🚀 Key Features

* **Decentralized Swarm Logic:** Nodes communicate via **ESP-NOW** (Latency < 10ms) without relying on a central server or internet connection for basic operations.
* **Predictive Lighting Corridor:** The system illuminates the path *ahead* of the vehicle based on shared tracking vectors, ensuring zero dark zones.
* **Automated Accident Detection:** Integrates **SW-420 Vibration Sensors** and **NEO-6M GPS** to detect impacts and log precise crash coordinates.
* **Emergency Response Protocol:**
    1.  **Local:** Immediate 2Hz Strobe/Flash effect to warn nearby drivers.
    2.  **Global:** Data uplink to the cloud via a Central Gateway for emergency dispatch.
* **Energy Efficiency:** Adaptive PWM control maintains lights at 20% brightness (Idle) and ramps to 100% only when needed.

## 🛠️ System Architecture

The project is built on a two-tier IoT architecture:

1.  **Tier 1: The Swarm (Edge Layer)**
    * Consists of individual Street Light Nodes.
    * Communicates offline via ESP-NOW.
    * Handles sensing and actuation.
2.  **Tier 2: The Gateway (Cloud Layer)**
    * A designated Hub that bridges the Swarm to the Internet.
    * Aggregates accident data and performs HTTP/MQTT POST requests to the backend.

## 🔌 Hardware Requirements

### Street Light Node (The Edge)
* **Microcontroller:** ESP32-WROOM-32 Dev Kit V1
* **Traffic Sensors:** HC-SR04 Ultrasonic Sensor + IR Proximity Sensor
* **Safety Sensors:** SW-420 Vibration Switch Module + NEO-6M GPS Module
* **Actuation:** 12V LED Array driven by IRF540 MOSFET
* **Power:** 12V DC Adapter + LM7805 Voltage Regulator

### Central Gateway (The Hub)
* **Microcontroller:** ESP32 NodeMCU
* **Connectivity:** Wi-Fi (for Cloud Uplink) or SIM800L GSM Module
* **Indicators:** Status LEDs / LCD Display

## 💻 Tech Stack & Libraries

* **Firmware:** C++ (Arduino IDE)
* **Communication:** `esp_now.h`, `WiFi.h`
* **GPS Parsing:** `TinyGPS++.h`
* **Network Protocols:** HTTP REST API (for Gateway)

## ⚙️ Installation & Setup

1.  **Clone the Repository**
    ```bash
    git clone [https://github.com/YOUR-USERNAME/Cooperative-Swarm-Street-Light.git](https://github.com/YOUR-USERNAME/Cooperative-Swarm-Street-Light.git)
    ```
2.  **Hardware Wiring**
    * Connect **HC-SR04** (Trig/Echo) to GPIO 5 & 18.
    * Connect **SW-420** to GPIO 19.
    * Connect **GPS** (TX/RX) to GPIO 16 & 17.
    * Connect **MOSFET Gate** to GPIO 2.
3.  **Flash the Firmware**
    * Open `Firmware/Street_Light_Node/Street_Light_Node.ino` in Arduino IDE.
    * Install required libraries via Library Manager.
    * Upload to all Street Light Nodes.
    * Open `Firmware/Central_Gateway_Hub/Central_Gateway_Hub.ino`.
    * Update `ssid` and `password` variables.
    * Upload to the Gateway Unit.

## 📊 Operational Workflow

1.  **Idle State:** System detects no motion. Lights dim to 20% brightness to conserve power.
2.  **Tracking State:** Ultrasonic sensor detects vehicle. Node brightens to 100% and sends a `PRE_WARN` signal to the next node to pre-light the path.
3.  **Panic State:** Vibration sensor detects impact (>4g). Node locks into `EMERGENCY_MODE`, strobes lights, and broadcasts GPS coordinates to the Hub.

## 👥 Contributors

**Team batch-37**
* *Project Lead & Firmware Architecture*
* *Hardware Integration & Circuit Design*
* *Documentation & Research*

---
*This project was developed as a Minor Project demonstrating the potential of Edge Computing in Smart Cities.*
