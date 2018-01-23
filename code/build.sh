avr-gcc -g -Os -mmcu=attiny85 -c $1.c
avr-gcc -g -mmcu=attiny85 -o $1.elf $1.o
avr-objcopy -j .text -j .data -O ihex $1.elf $1.hex
cp $1.hex demo.hex
