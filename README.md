# ❤️ Real-Time Health Monitoring System

A smart IoT-based healthcare monitoring system that continuously tracks vital health parameters and detects emergency situations in real time.

## 📌 Overview

This project uses ESP32 and multiple sensors to monitor:

* ❤️ Heart Rate (BPM)
* 🩸 Blood Oxygen Level (SpO₂)
* 🌡️ Body Temperature
* 🚶 Fall Detection
* 🚨 Emergency Alerts

The system displays data on an OLED screen, sends real-time notifications through Blynk IoT, and activates visual/audio alarms when abnormal conditions are detected.

---

## 🚀 Features

### Health Monitoring

* Real-time Heart Rate Monitoring
* Real-time SpO₂ Monitoring
* Temperature Monitoring
* OLED Display Output

### Safety Features

* Fall Detection using MPU6050
* Emergency Alert Button
* Buzzer Alarm System
* LED Status Indicators

### IoT Integration

* Blynk Cloud Connectivity
* Remote Monitoring
* Real-Time Notifications
* Emergency Event Logging

### Performance Optimization

* ESP32 FreeRTOS Multitasking
* Dedicated Sensor Tasks
* Protected I2C Communication
* Stable Sensor Data Processing

---

## 🛠 Hardware Components

| Component   | Purpose                  |
| ----------- | ------------------------ |
| ESP32       | Main Controller          |
| MAX30100    | Heart Rate & SpO₂ Sensor |
| MPU6050     | Fall Detection Sensor    |
| DHT11       | Temperature Sensor       |
| SH1106 OLED | Display Output           |
| Buzzer      | Audio Alert              |
| LEDs        | Visual Alerts            |

---

## 📊 System Architecture

Sensor Data Collection
↓
ESP32 Processing
↓
Health Analysis
↓
OLED Display
↓
Blynk Cloud
↓
Mobile Notifications

---

## 🔔 Alert Conditions

### Temperature Alert

* Below 27°C
* Above 38°C

### Heart Rate Alert

* Below 50 BPM
* Above 120 BPM

### SpO₂ Alert

* Below 92%

### Fall Detection

* Sudden acceleration changes
* Significant orientation changes

---

## 📱 Blynk Dashboard

The system sends:

* Health Data Updates
* Fall Detection Alerts
* Temperature Alerts
* Heart Rate Alerts
* SpO₂ Alerts
* Emergency Notifications

---

## 💻 Technologies Used

* Embedded C++
* Arduino IDE
* ESP32
* FreeRTOS
* Blynk IoT Platform
* I2C Communication

---

## 📸 Project Preview

Add your project images here:

* Hardware Setup
* OLED Output
* Blynk Dashboard
* System Flow Diagram

---

## 🎯 Future Improvements

* ECG Monitoring
* GSM SMS Alerts
* Cloud Data Storage
* AI-Based Health Prediction
* Medical Report Generation
* Mobile Application

---

## 👩‍💻 Developer

**Maksuda Parvin**

Computer Science & Engineering (CSE)

Bangladesh University of Business and Technology (BUBT)

GitHub: https://github.com/MaksudaParvin

---

⭐ If you find this project useful, consider giving it a star.
