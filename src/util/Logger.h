#pragma once
#include <Arduino.h>

#define LOG_INFO(fmt, ...)  do { Serial.printf("[I] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_WARN(fmt, ...)  do { Serial.printf("[W] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_ERROR(fmt, ...) do { Serial.printf("[E] " fmt "\n", ##__VA_ARGS__); } while(0)
