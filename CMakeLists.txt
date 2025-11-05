cmake_minimum_required(VERSION 3.16)

# Set partition table
set(PARTITION_TABLE_CSV "${CMAKE_SOURCE_DIR}/partitions.csv")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project
project(esp32-x402-client)