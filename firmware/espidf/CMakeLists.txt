# ESP-IDF Project for ESP32 Voice Hub
# Waveshare ESP32-S3-Knob-Touch-LCD-1.8

cmake_minimum_required(VERSION 3.16)

# Include ESP-SR for wake word detection
set(EXTRA_COMPONENT_DIRS 
    $ENV{IDF_PATH}/examples/common_components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_voice_hub)
