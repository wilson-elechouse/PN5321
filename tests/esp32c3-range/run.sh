#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I. \
  test_uart_range.cpp \
  ../../extras/esp32c3-range-test/PN532_All_Cards_UART_Range_Test/Pn532Hsu.cpp \
  -o build/test_uart_range
./build/test_uart_range
