cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32s3 CACHE STRING "B board MCU")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(laser_harp_b)
