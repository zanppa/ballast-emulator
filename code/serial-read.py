#!/usr/bin/env python

# serial-read.pu
# Reads the ballast emulator responses to queries from serial-send.py
# Uses pigpio software UART to allow using any pin as UART
# Serial format is 2400 baud, 8 data bits, even parity

# Lauri Peltonen, 2018

# Modified dramatically from https://raspberrypi.stackexchange.com/questions/27488/pigpio-library-example-for-bit-banging-a-uart
# Original license:
# bb_serial.py
# 2014-12-23
# Public Domain

import time
import pigpio

# Raspberry PI pin to use for reading, same as the programming interface MISO
RX = 25

baud = 2400
bits = 9

pi = pigpio.pi()

# fatal exceptions off (so that closing an unopened gpio doesn't error)
pigpio.exceptions = False
pi.bb_serial_read_close(RX)

# fatal exceptions on
pigpio.exceptions = True

# open a gpio to bit bang read the echoed data
pi.bb_serial_read_open(RX, baud, bits)

while 1:
   (count, data) = pi.bb_serial_read(RX)
   if count > 0:
      # Print received data in HEX
      print '[{}]'.format(', '.join(hex(x) for x in data))

   time.sleep(0.1) # enough time to ensure more data


pi.bb_serial_read_close(RX)
pi.stop()
