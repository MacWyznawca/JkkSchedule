cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Arduino as ESP-IDF component — JkkSchedule example
# Status: NOT TESTED
#
# This is a standard ESP-IDF project. Arduino-ESP32 is added as an extra
# component so you can use Serial, millis(), etc. inside ESP-IDF code.
#
# Prerequisites:
#   1. Clone arduino-esp32 into components/:
#      git clone https://github.com/espressif/arduino-esp32 components/arduino
#
#   2. (Option A) Clone JkkSchedule into components/:
#      git clone https://github.com/MacWyznawca/JkkSchedule components/JkkSchedule
#
#      (Option B) Let ESP-IDF component manager download it automatically
#      via the dependency declared in main/idf_component.yml.
#
#   3. Build normally:
#      idf.py set-target esp32s3
#      idf.py build
# ---------------------------------------------------------------------------

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arduino_jkkschedule_example)
