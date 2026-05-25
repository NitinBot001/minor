# Autonomous Medical Assistant Robot 🏥🤖

**Minor Project - 2 | Electronics and Communication Engineering** **Rewa Engineering College**

An advanced, IoT-enabled autonomous medical assistant robot designed for contactless patient monitoring in hospital wards. The system utilizes a multi-microcontroller architecture (Rover ESP32, Mother ESP32, Arduino UNO) to independently navigate to patient beds, provide graphical touch-screen instructions, collect live vitals (SpO2, HR, Temp, ECG), and seamlessly upload the data to a globally accessible secure cloud dashboard.

---

## 🚀 Key Features

* **Autonomous Time-Distance Navigation:** Eliminates optical sensor failures by using a highly reliable, push-button-triggered 4-second distance layout calibrated at `120 PWM` base speed.
* **Super-Fast Vitals Engine:** Optimized MAX30102 algorithm with a reduced detection threshold (`6000`) and an 8-sample locking system to freeze and display results in under 4 seconds.
* **20-Second Live ECG Buffering:** The system reads AD8232 chest leads and creates a 500-data-point RAM buffer at 25Hz on the ESP32, preventing serial overflow before pushing to the cloud.
* **Interactive Resistive Touch UI:** A 15-page graphical state machine running on an Arduino UNO with physically calibrated touch matrix coordinates for flawless patient interaction.
* **Touchless Sanitizer Dispenser:** Uses an ultrasonic sensor (HC-SR04) and relay module mapped to `FreeRTOS Core 1`, allowing 2-second fluid dispensing without blocking vital sensors.
* **Global HTTPS Dashboard:** Secure payload delivery to a Python Flask + SQLite backend hosted on **Hugging Face Spaces**, featuring Chart.js for rendering live ECG traces.

---

## 🧠 System Architecture & Communication Flow

The project is distributed across three processing units working in perfect synchronization via Handshaking and Serial Communication.

```text
[ ROVER ESP32 ] <--- (Digital Handshake Pins) ---> [ MOTHER ESP32 ] <--- (UART Serial) ---> [ ARDUINO UNO ]
   (Motors)                                           (Sensors)                               (TFT UI)
                                                          |
                                                  (HTTPS POST)
                                                          v
                                            [ Hugging Face Web Server ]

```

---

## 🔌 Complete Hardware Connection Guide

### 1. Mother ESP32 (Master Orchestrator & Cloud Hub)

Handles all health sensors, Wi-Fi, HTTPS sync, and FreeRTOS tasks.

| Component / Target | Mother ESP32 Pin | Purpose |
| --- | --- | --- |
| **Arduino UNO TX** | GPIO 16 (RX2) | Receives UI Sync/Next commands via SoftSerial. |
| **Arduino UNO RX** | GPIO 17 (TX2) | Streams live vitals `(B: S: T: E:)` to the display. |
| **Rover ESP32** | GPIO 14 (Input) | Receives "Arrived at Bed" pulse from Rover. |
| **Rover ESP32** | GPIO 27 (Output) | Sends "Move to Next Bed" trigger to Rover. |
| **MAX30102** | GPIO 21 (SDA), 22 (SCL) | I2C Fast Mode (400kHz) for HR & SpO2. |
| **AD8232 (ECG)** | GPIO 34 (OUT) | Reads analog ECG waveforms. |
| **AD8232 (Leads)** | GPIO 32 (LO+), 33 (LO-) | Lead-off detection digital inputs. |
| **Thermistor (10K)** | GPIO 35 | Analog input for body temperature calculation. |
| **HC-SR04 (Sonar)** | GPIO 12 (Trig), 13 (Echo) | Proximity detection for hands. |
| **12V Relay Module** | GPIO 15 | Controls the sanitizer fluid pump. |

### 2. Rover ESP32 (Chassis & Navigation Controller)

Operates the L298N driver and executes the precision time-distance engine.

| Component | Rover ESP32 Pin | Purpose |
| --- | --- | --- |
| **Launch Button** | GPIO 2 | First-time manual system start (Pull-up to GND). |
| **Mother ESP32** | GPIO 4 (Output) | Notifies Mother upon reaching the target distance. |
| **Mother ESP32** | GPIO 5 (Input) | Waits for Mother's command to move to the next bed. |
| **L298N Motor A** | ENA=21, IN1=22, IN2=23 | Left motor speed and direction control. |
| **L298N Motor B** | ENB=25, IN3=26, IN4=27 | Right motor speed and direction control. |

### 3. Arduino UNO (UI Engine)

* Mount the standard **2.4/2.8 inch TFT LCD Shield** directly onto the UNO headers.
* **Pin 1 (Hardware TX):** Connect to Mother ESP32 GPIO 16 (RX2).
* **Pin A5 (Software RX):** Connect to Mother ESP32 GPIO 17 (TX2).

⚠️ **CRITICAL SAFETY NOTE:** The `GND` pins of the Rover ESP32, Mother ESP32, and Arduino UNO **must be common (connected together)**. Failure to do so will result in corrupted serial data and failed handshaking.

---

## 👥 Team Members & Defense Briefing

**Team Members:** Jaya Dubey, Nitin Bhujwa, Siddhant Soni.

### Role Allocation

* **Jaya Dubey:** Developed the 15-page TFT state-machine UI, resistive touch coordinate mapping, clinical testing workflows, and aesthetic hardware assembly.
* **Nitin Bhujwa:** Handled core firmware integration, dynamic array buffering (ECG), cross-MCU handshaking logic, and production cloud/dashboard deployment on Hugging Face.
* **Siddhant Soni:** Managed chassis mechanics, time-distance calibration calculations, L298N motor integration, and overall internal power distribution (12V and 5V isolation).

### Expected Viva Questions & Answers

**Q1: How did you prevent data overflow when transmitting 20 seconds of ECG data to the cloud?** > *Ans:* Sending 500 data points point-by-point to the Arduino UNO would crash its limited serial buffer. Instead, the UNO merely sends a `CMD:START_ECG` trigger. The Mother ESP32 logs the 500 integer points into its own local RAM array and securely packages it into a single JSON payload for the HTTPS cloud push.

**Q2: Why use a Time-Distance navigation method instead of an optical PID line follower?** > *Ans:* Optical IR sensors are highly susceptible to ambient light interference and floor texture variations, which causes unreliability during live demonstrations. A time-distance engine (`4000ms at constant PWM`) bypasses sensor dependency and provides a 100% fail-safe and repeatable transit route.

**Q3: How does the Sanitizer pump run without freezing the live ECG graph on the screen?** > *Ans:* The ESP32 is a dual-core processor. The primary health sensors, ECG logging, and Wi-Fi run on `Core 0`. The ultrasonic proximity detection and relay delays for the sanitizer are pinned independently to a FreeRTOS task on `Core 1`. This allows the pump to dispense for 2 seconds without blocking the real-time vitals thread.

---

## 🛠️ Software Requirements & Libraries

* **Arduino IDE** (ESP32 Board Package installed)
* `SparkFun MAX3010x Pulse and Proximity Sensor Library`
* `LCDWIKI_GUI` and `LCDWIKI_KBV` (For TFT Graphics)
* `Adafruit TouchScreen` (For touch input)
* Python 3.x, Flask, SQLite3 (For Backend)

## ⚙️ Setup & Execution

1. Host the `server.py` script on your preferred cloud platform (e.g., Hugging Face Spaces).
2. Update the `WIFI_SSID`, `WIFI_PASSWORD`, and `SERVER_URL` in the `mother_esp32.ino` code.
3. Upload code to all three microcontrollers respectively. *(Remember to unplug Pin 1 on the UNO while uploading).*
4. Power up the system, wait for the standby screen, and press the physical push-button on the Rover to launch the autonomous sequence!
