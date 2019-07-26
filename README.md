# ballast-emulator
Ballast emulator to allow running a video projector without the lamp.

This project provides code for Attiny85 to emulate a "3-wire" or "Ushio ballast" to allow a 
projector (e.g. Canon LV-X7 whivh I used) to turn on without a lamp (or the original ballast).
This allows using a custom lamp or use the LCD or whatever for other purposes.

## Operation mode
The operation mode is set by pull-up or pull-down resistors on pins `PB3` (ID0) and `PB4` (ID1). 
Following table shows how the modes are selected, 0=pulled down, 1=pulled up.

| Mode   | PB3, ID0 | PB4, ID1 |
| ------ |:--------:| :-------:|
| Dead   | 0        | 0        |
| 3-wire | 1        | 0        |
| Osram  | 0        | 1        |
| Ushio  | 1        | 1        |

Both ID pins have internal pull-ups enabled, so leaving the pins unconnected selectes the Ushio mode by default.

## Dead
This mode disables all pull-ups and configures pins as inputs and then sits doing nothing. 
This can be used to probe the signals or use some external tool to debug/hack the signals 
even with the emulator connected.

## 3-wire ballast
The 3-wire ballast has 3 digital pins for communication: power, dim and sync. The pins are connected as follows.

| Pin number | Name | Use | Direction |
| :--------: | :---: | --- | --------- |
| 5 | PB0 | DIM |input |
| 6 | PB1 | PWR | output |
| 7 | PB2 | Sync | input |
 
 The `Sync` pin indicates that the projector wants to turn the lamp on. In this mode the emulator responds 
 by setting the `PWR` pin high which indicates that the lamp is powered.
 
 The `DIM` pin is used to indicate that the projector wants to dim the lamp. The emulator does nothing with this pin.
 
 
## Osram
This is similar serial communication protocol to Ushio, but is currently not implemented. Setting this mode does nothing.

## Ushio
The Ushio ballast uses a serial communication operating at 2400 bps, 8 data bits, 1 even parity bit, 1 stop bit. The pinout is according to following list.

| Pin number | Name | Use | Direction |
| :--------: | :---: | --- | --------- |
| 5 | PB0 | RX |input |
| 6 | PB1 | TX | output |
| 7 | PB2 | Power flag | input |

The `Power flag` is not used in the emulator.

The serial communication is handled by a very simple software UART implementation. I have no idea what the messages sent by the projector mean, and the replies are sniffed by [people](http://www.eevblog.com/forum/beginners/video-projector-ballast-bypass-help/) having a working projector (my projector was without lamp so I could only sniff projector messages). I only corrected the decoding presented in the thread. Following message-reply pairs are implemented, and they seem to be enough to turn the projector on.
```
Message from projector     Ballast reply
0x51 0x0D                  0x51 0x32 0x0D
0x4C 0x46 0x0D             0x41 0x0D
0x50 0x0D                  0x50 0x46 0x0D
0x51                       0x51 0x32 0x0D
0x4C 0x45 0x0D             0x41 0x0D -- This is probably incorrect reply
```
