/**
 * Ballast emulator for projectors
 *
 * Emulates "3-wire", Ushio serial [and Osram serial (maybe in the future)] protocols
 *
 * Works with Attiny85 and custom optoisolator board
 *
 * Copyright (C) 2016-2018 Lauri Peltonen
 */

// Run on internal 8 MHz RC oscillator
#define F_CPU 8e6


#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

// Echo RX directly to TX
//#define DEBUG_ECHO

// Blink led when data is received
#define DEBUG_LED


/**
 * Attiny85 programming pins
 *  1, /RESET     -- "input"
 *  5, PB0, MOSI  -- input
 *  6, PB1, MISO  -- output
 *  7, PB2, CLK   -- input
 *
 * Data pins used for emulator
 * Projector must be OFF for programming, so that
 * no data flows through optoisolators, since same
 * pins are used for programming and data communication
 *
 *  N  Name Ushio/Osram Flag
 *  5, PB0, RX          DIM   -- input
 *  6, PB1, TX          PWR   -- output
 *  7, PB2, Power flag  Sync  -- input
 *
 */

#define RXPIN		0x01	// Bit 1, PB0, DIM/RXD (in A)
#define TXPIN		0x02	// Bit 2, PB1, FLAG/TXD (out)
#define SYNCPIN		0x04	// Bit 3, PB2, SCI/SYNC (in B)
#define ID0		0x08	// Bit 4, PB3
#define ID1		0x10	// Bit 5, PB4

#define OUTPUTPINS	(TXPIN)
#define INPUTPINS	(RXPIN | SYNCPIN | ID0 | ID1)
#define PULLUPS		(RXPIN | SYNCPIN | ID0 | ID1)

// Timer tick indicator to set operation speed correctly
volatile uint8_t timerTriggered = 0;

// Mode, set by the ID bits
typedef enum {DEAD = 0x00, FLAG = 0x01, OSRAM = 0x02, USHIO = 0x03} mode_t;

typedef enum {IDLE, START, DATA, PARITY, STOP} uartState_t;

// Timeout if complete command is not received
#define UART_RX_TIMEOUT		480			// 480 cycles @  4x2400 baud = 50 ms

// Queries from projector to USHIO ballast, and replies to those
#define USHIO_MAX_COMMAND 	5
#define USHIO_QUERIES		5
typedef struct {
	unsigned char qLength;		// Query length
	unsigned char qData[USHIO_MAX_COMMAND];	// Query data
	unsigned char rLength;		// Reply length
	unsigned char rData[USHIO_MAX_COMMAND];	// Reply data
} ushioQuery_t;
ushioQuery_t ushioQuery[USHIO_QUERIES] = {
	{2, "\x51\x0D", 3, "\x51\x32\x0D"},
	{3, "\x4C\x46\x0D", 2, "\x41\x0D"},
	{2, "\x50\x0D", 3, "\x50\x46\x0D"},
	{2, "\x51\x0D", 3, "\x51\x32\x0D"},
	{3, "\x4C\x45\x0D", 2, "\x41\x0D"},	// TODO: This is probably wrong reply!
	};

// Receive buffer
#define UARTRXMASK		0x0F	// Buffer length = 16 bytes
uint8_t uartRxBuffer[16] = {0};
uint8_t uartRxWrite = 0;		// Write position (write to buffer)
uint8_t uartRxRead = 0;			// Read position (read from buffer)

// Transmit buffer
#define UARTTXMASK		0x0F	// Buffer length = 16 bytes
uint8_t uartTxBuffer[16] = {0x51, 0x32, 0x0D, 0x00, 0x00};
uint8_t uartTxWrite = 0;		// Write position (write to buffer)
uint8_t uartTxRead = 0;			// Read position (read from buffer, write to output)


/**
 * Interrupt handler for timer 1
 * Triggers when timer reaches the compare value
 *
 * Sets the trigger flag to 1, loops wait for this flag
 * This clears the interrupt flag
 */
ISR (TIMER1_COMPA_vect)
{
	timerTriggered = 1;
}

// This routine handles the USHIO serial communication
// USHIO driver uses a 2400 baud serial communication
// with bus idling high (1), then 1 start bit (0),
// 8 data bits (LSB first?), 1 parity bit (if data has odd 1s, then parity is 1),
// and 1 stop bit (1).
// Currently this only handles half-duplex communication, i.e. if data is received during transmit, it is not parsed
void ushioLoop(void)
{
	uint8_t rxLastBit = 0;
	uint8_t rxBit = 0;
	uartState_t uartRxState = IDLE;
	uint8_t rxParity = 0;
	uint8_t rxTick = 0;
	uint8_t readBit = 1;	// Bit number

	uint8_t txBit = 1;	// Bus idles high
	uartState_t uartTxState = IDLE;
	uint8_t txParity = 0;
	uint8_t txTick = 0;
	uint8_t writeBit = 1;	// Bit number

	uint16_t rxTimeout = 0;	// Command timeout / synchronisation

	uint8_t i, j, failed;

	while(1)
	{
		// Wait for timer, everything is done after clock pulse
		while(!timerTriggered);
		timerTriggered = 0;

		// Check status on RXD
		rxLastBit = rxBit;	// Store current bit to detect edges
		rxBit = PINB & RXPIN;

		// Write correct level (or keep existing) to TXD
#ifdef DEBUG_ECHO
		if(rxBit)
			PORTB |= TXPIN;
		else
			PORTB &= ~TXPIN;
#else
		if(txBit)
			PORTB |= TXPIN;
		else
			PORTB &= ~TXPIN;
#endif

		// Timers for read & write
		if(rxTick) rxTick--;
		if(txTick) txTick--;
		if(rxTimeout) rxTimeout--;

		// uart RX is handled only on certain ticks (when rxTick == 0)
		if(!rxTick) {
			// Also check last bit so does not trigger if
			// signal idles low for some reason
			if(uartRxState == IDLE && rxBit == 0 && rxLastBit == 1) {
				// Start bit received
				uartRxState = START;
				rxTick = 1;		// Verify start on next tick
			} else if(uartRxState == START) {
				if(rxBit == 0) {
					// Still zero -> OK, start reading
					rxParity = 0;
					readBit = 1;		// LSB first
					uartRxState = DATA;
					uartRxBuffer[uartRxWrite] = 0;		// Clear byte
					rxTick = 4;		// Read after 1 complete cycle => 2400 baud

					// Set up timeout
					if(!rxTimeout) rxTimeout = UART_RX_TIMEOUT;
				} else {
					// Glitch probably? Back to idle
					uartRxState = IDLE;
					rxTick = 0;
				}
			} else if(uartRxState == DATA) {
				// Handle data bits
				// Turn bit to 1 if data bus was high
				if(rxBit) {
					uartRxBuffer[uartRxWrite] |= readBit;
					rxParity ^= 1;		// Toggle expected parity bit (even parity)
				}
				readBit <<= 1;		// Next bit
				if(!readBit) {
					// Overflow -> last bit was just read
					uartRxState = PARITY;		// Next up is parity bit
					uartRxWrite = (uartRxWrite + 1) & UARTRXMASK;	// Change write position in buffer
				}

				rxTick = 4;			// Schedule next bit
			} else if(uartRxState == PARITY) {
				// Parity bit, currently do not care
				if(rxBit != rxParity) {;}	// TODO: Handle parity error?
				uartRxState = STOP;
				rxTick = 4;
			} else {
				// Stop bit
				if(!rxBit) {;} 		// TODO: If no STOP bit, handle the error?
				uartRxState = IDLE;
				rxTick = 0;		// Next falling edge instantaneously trigs new receive
			}
		}

		// TX is handled similarly, but is a bit simpler
		if(!txTick) {
			txTick = 4;		// Everything happens at the same baud rate

			if(uartTxState == START) {
				txBit = 0;			// Start bit always low
				writeBit = 1;
				txParity = 0;
				uartTxState = DATA;
			} else if(uartTxState == DATA) {
				// Handle data bits
				if(uartTxBuffer[uartTxRead] & writeBit) {
					txBit = 1;
					txParity ^= 1;		// Toggle expected parity bit (even parity)
				} else {
					txBit = 0;
				}
				writeBit <<= 1;		// Next bit
				if(!writeBit) {
					// Overflow -> last bit was just read
					uartTxState = PARITY;		// Next up is parity bit
					uartTxRead = (uartTxRead + 1) & UARTTXMASK;	// Change read position in buffer
				}
			} else if(uartTxState == PARITY) {
				// Send parity bit
				txBit = txParity;
				uartTxState = STOP;
			} else {
				// Stop bit
				txBit = 1;		// Stop bit always high
				uartTxState = IDLE;
			}
		}

		// Get ready to send next byte if in buffer
		if(uartTxState == IDLE) {
			if(uartTxRead < uartTxWrite) {
				// New data in buffer
				uartTxState = START;	// Start sending
			} else {
				// Clear buffer
				uartTxRead = 0;
				uartTxWrite = 0;
			}
		}

		// Check received data and send response if necessary
		if(uartRxState == IDLE && uartRxWrite > 0) {
			// Bytes in buffer and bus is idle
			// Check if any message matches the received data

			failed = 0;
			for(i=0; i<USHIO_QUERIES; i++){
				failed &= 0xFE;		// Clear lowest bit
				if(uartRxWrite >= ushioQuery[i].qLength) {
					// Received enough bytes, check contents
					for(j=0; j<ushioQuery[i].qLength; j++) {
						if(uartRxBuffer[j] != ushioQuery[i].qData[j]){
							failed |= 0x01;		// bit 1 => this data did not match
							break;
						}
					}

					if(!(failed & 0x01)) {
						// All bytes match -> add response to buffer

						// Check that buffer has space
						if(UARTTXMASK > uartTxWrite + ushioQuery[i].rLength) {
							for(j=0; j<ushioQuery[i].rLength; j++) {
								// Append response to buffer
								uartTxBuffer[uartTxWrite++] = ushioQuery[i].rData[j];
							}
						}

						// Clear fail bit to indicate that the message was handled
						// Ie RX buffer can be cleared even though some messages may be longer still
						failed = 0;

						// Break out of the for loop
						break;
					}
				} else {
					failed |= 0x02; // Some messages are longer than what is received so far
				}
			}

			// Messages were checked
			// Clear receive buffer if message was handled or no longer messages are expected
			// Also clear the receive buffer if message has timeout'd
			if(failed == 0 || !(failed & 0x02) || rxTimeout == 0) {
				uartRxWrite = 0;
				uartRxRead = 0;
				rxTimeout = 0;	// Ready for next messages that were not timeout'd before
			}
		}
	}
}



// This routine handles the OSRAM serial communication
// TODO: To be implemented
void osramLoop(void)
{
	while(1)
	{
		;
	}
}

// This routine handles the simple lamp on / dim / flag communication scheme
void flagLoop(void)
{
	uint8_t pin_status = 0;
	uint8_t lamp_on = 0;
	uint8_t dim_on = 0;

	while(1)
	{
		// Wait for timer, just to not run as fast as possible
		while(!timerTriggered);
		timerTriggered = 0;

		pin_status = PINB;

		// Check DIM/RXD; if pin is low, then dim the lamp
		if(!(pin_status & RXPIN))  {
			dim_on = 1;
		} else {
			dim_on = 0;
		}

		// Check SCI/Sync
		// If pin is low, turn lamp ON
		// Flag follows the request to turn on the light immediately
		if(!(pin_status & SYNCPIN))
		{
			lamp_on = 1;
			PORTB |= TXPIN;
		} else {
			lamp_on = 0;
			PORTB &= ~TXPIN;
		}

	}
}

/**
 * Main function
 *
 */
int main(void)
{
	uint8_t modeBits = 0;

	// Default mode
	mode_t operationMode = DEAD;

	// Set clock speed to 8 MHz
	// By default the internal RC is 8 MHz
        CLKPR = (1 << CLKPCE);  	// Enable prescaler change
        CLKPR = 0;      		// Prescale to 1 => 8 MHz

	// Initialize everything as input with pull-ups
	// Internal pull-up on I/Os is 20...50 kOhm
	// So if pulled low with e.g. 4.7 k will result in 0.2 V minimum
	// Must be less than about 1.3 V to read as 0 -> OK
	DDRB = 0;	// All pins as input

	// Enable pull-ups on other pins than TX (input pins)
	// since the input signals idle high
	// This is also needed to ensure USHIO mode if no external resistors on pins
	PORTB = PULLUPS;

	_delay_ms(1);	// Wait 1 ms to ensure pull-ups are stable

	// Read GPIO to determine operation mode
	// ID0 sets the bit 0, ID1 sets bit 1
	modeBits = PINB & (ID0 | ID1);
	operationMode = ((modeBits & ID0) ? 0x01 : 0x00) | ((modeBits & ID1) ? 0x02 : 0x00);

	// If operation mode is DEAD (e.g. for debugging with external
	// hardware in the RX/TX pins, hang here)
	// Also disable the pull-ups just in case
	if(operationMode == DEAD) {
		PORTB = 0x00;	// Disable pull-ups
		while(1);	// Stay, dog
	}

	// Set GPIO outputs on port B
	// 1 = Output, so set only TXPIN
	// PORTB is already 0 so output goes low without glitch
	DDRB = OUTPUTPINS;

	// Configure timer
	// For Osram 9600 baud this is 4 ticks/bit, for Ushio 2400 baud 16 ticks/bit
        TCCR1 = 0;		// Reset timer value
        TCNT1 = 0;		// Preset timer value to 0
        GTCCR = (1 << PSR1);	// Reset the prescaler

	// Select the clock speed
	if(operationMode == OSRAM) {
	        OCR1A = 26;		// 26 us (9600 bps/4) for OSRAM
	        OCR1C = 26;
	} else {
	        OCR1A = 104;		// 104 us (2400 bps/4) for others
	        OCR1C = 104;
	}

        TIMSK = (1 << OCIE1A);	// Enable interrupt on compare 0A match

        // Start the timer in compare output mode
        // PLLCSR = (1 << PLLE) | (1 << PLOCK);
        TCCR1 = (1 << CTC1) | (1 << CS12);   // Prescaler, tick is CLK / 8 = 1 MHz

	// Enable global interrupts
	sei();

	// Select correct operation loop
	switch(operationMode) {
	case USHIO:
		ushioLoop();
		break;
	case OSRAM:
		osramLoop();
		break;
	case FLAG:
		flagLoop();
		break;
	case DEAD:
	default:
		break;
	}

	while(1);	// Loop forever, should not end here

	return 0;
}
