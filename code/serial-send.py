#!/usr/bin/env python

# serial-send.py
# Emulates projector and sends queries to the ballast emulator
# Uses pigpio software UART to allow using any pin as UART
# Serial format is 2400 baud, 8 data bits, even parity

# Lauri Peltonen, 2018

# Modified dramatically from https://raspberrypi.stackexchange.com/questions/27488/pigpio-library-example-for-bit-banging-a-uart
# Original license:
# bb_serial.py
# 2014-12-23
# Public Domain


import pigpio

# Raspberry PI transmit pin, same as the programming interface MOSI
TX = 24

# 2400 baud, 8 data bits, 1 even parity bit (bits in data is odd => parity is even), 1 stop bit
baud = 2400
bits = 9

# Calculate parity
# from http://p-nand-q.com/python/algorithms/math/bit-parity.html
def parallel_swar(i):
    i = i - ((i >> 1) & 0x55555555)
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333)
    i = (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24
    return int(i % 2)

parity_lookup = [parallel_swar(i) for i in range(256)]

def parity(v):
    v ^= v >> 16
    v ^= v >> 8
    return parity_lookup[v & 0xff]

# initialize test data
msg_orig = [[0x4C, 0x46, 0x0D], [0x51, 0x0D], [0x50,0x0D], [0x4c, 0x45, 0x0D]]
current = 0
messages = len(msg_orig)

pi = pigpio.pi()
pi.set_mode(TX, pigpio.OUTPUT)

# fatal exceptions on
pigpio.exceptions = True


while 1:	# Replaced runtime-thing with this
   raw_input("Send bytes")

   # Create the serial waveform
   msg = []
   # Append parity bits
   for i in range(len(msg_orig[current])):
      msg.append(msg_orig[current][i])
      msg.append(parity(msg_orig[current][i]))

   # create a waveform representing the serial data
   pi.wave_clear()
   pi.wave_add_serial(TX, baud, msg, bb_bits=bits)
   wid=pi.wave_create()

   pi.wave_send_once(wid)   # transmit serial data

   print '[{}]'.format(', '.join(hex(x) for x in msg))

   while pi.wave_tx_busy(): # wait until all data sent
      pass

   current += 1
   if current >= messages:
      current = 0

pi.wave_delete(wid)
pi.stop()
