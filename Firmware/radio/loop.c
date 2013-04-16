// -*- Mode: C; c-basic-offset: 8; -*-
//
// Copyright (c) 2012 Andrew Tridgell, All Rights Reserved
// Copyright (c) 2011 Michael Smith, All Rights Reserved
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  o Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  o Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//

///
/// @file	loop.c
///
/// main loop
///

#include <stdarg.h>
#include "radio.h"
#include "loop.h"
#include "timer.h"
#include "freq_hopping.h"

/// a packet buffer for the LOOP code
__xdata uint8_t	pbuf[MAX_PACKET_LENGTH];

// a stack carary to detect a stack overflow
__at(0xFF) uint8_t __idata _canary;


static uint8_t swap_bit_order(register uint8_t b) 
{
    /*
     * The data bytes come over the air from the ISS least significant bit first.  Fix them as we go. From
     * http://www.ocf.berkeley.edu/~wwu/cgi-bin/yabb/YaBB.cgi?board=riddles_cs;action=display;num=1103355188
     */
    b = ((b & 0b11110000) >>4 ) | ((b & 0b00001111) << 4);
    b = ((b & 0b11001100) >>2 ) | ((b & 0b00110011) << 2);
    b = ((b & 0b10101010) >>1 ) | ((b & 0b01010101) << 1);
    return b;
}


static void 
swap_packet_bit_order(__pdata uint8_t len)
{
	__pdata uint8_t i;
	for (i=0; i<len; i++) {
		pbuf[i] = swap_bit_order(pbuf[i]);
	}
}

/*
  find CRC16 of pbuf
 */
static uint16_t crc16_ccitt(uint8_t len)
{
	register uint8_t i, j;
	register uint16_t crc = 0;
	for (i=0; i<len; i++) {
		register uint8_t b = pbuf[i];
		crc ^= b << 8;
		for (j=0; j<8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ 0x1021;
			} else {
				crc = crc << 1;
			}
		}
	}
	return crc;
}

// bit definitions for valid_mask, telling us what
// types of data have been received
#define VALID_RECV_PACKETS   (1<<0)
#define VALID_LOST_PACKETS   (1<<1)
#define VALID_BAD_CRC        (1<<2)
#define VALID_RSSI           (1<<3)
#define VALID_TRANSMITTER    (1<<4)
#define VALID_WIND_SPEED     (1<<5)
#define VALID_WIND_DIRECTION (1<<6)
#define VALID_TEMPERATURE    (1<<7)
#define VALID_LIGHT          (1<<8)
#define VALID_RAIN_SPOONS    (1<<9)
#define VALID_HUMIDITY       (1<<10)
#define VALID_UVI            (1<<11)
#define VALID_SOLAR          (1<<12)

#define ISS_DATA_VERSION "1.0"

/*
  state of the data received from ISS
 */
__xdata static struct {
	uint16_t valid_mask;
	uint8_t raw[10];	
	uint8_t transmitter_id;
	uint8_t rssi;
	uint32_t lost_packets;
	uint32_t recv_packets;
	uint32_t bad_crc;

	uint8_t wind_speed_mph;
	uint16_t wind_direction_degrees;
	float temperature_F;
	uint16_t light;
	uint8_t rain_spoons;
	float humidity_pct;
	float uv_index;
	float solar_wm2;
} iss_data;

static __pdata uint8_t one_second_counter;
static __pdata uint32_t seconds_since_boot;
static __pdata uint32_t seconds_last_packet;

static void one_second(void)
{
	one_second_counter++;
	seconds_since_boot++;
	if (seconds_last_packet == 0 || (seconds_since_boot - seconds_last_packet) > 10) {
		if (one_second_counter > 51) {
			fhop_prev();
			radio_set_frequency(fhop_receive_freqency());
			radio_receiver_on();
			one_second_counter = 0;
		}
		printf("Searching %lu at %lu Hz\n", 
		       (unsigned long)seconds_since_boot,
		       (unsigned long)fhop_receive_freqency());
		return;
	}
	if (one_second_counter >= 3) {
		fhop_next();
		radio_set_frequency(fhop_receive_freqency());
		radio_receiver_on();
		one_second_counter = 0;
		if (seconds_last_packet != 0) {
			iss_data.lost_packets++;
			iss_data.valid_mask |= VALID_LOST_PACKETS;
		}
	}
}

// print a float to 1 digits
static void print_float1(float v)
{
	register int v1 = v;
	register int v2 = (v - v1)*10;
	printf("%d.%u", v1, v2);
}

// print a float to 2 digits
static void print_float2(float v)
{
	register int v1 = v;
	register int v2 = (v - v1)*100;
	printf("%d.%u", v1, v2);
}

// print a 2 digit hex value
static void print_hex(register uint8_t v)
{
	// this avoids a problem with %02x in printf library
	if (v < 16) {
		printf("0");
	}
	printf("%x", (unsigned)v);
}

// display ISS data as JSON
static void show_iss_data(void)
{
	__pdata uint8_t i;
	printf("{ ");
	if (iss_data.valid_mask & VALID_TRANSMITTER) {
		printf("\"transmitter_id\": %u, ", (unsigned)iss_data.transmitter_id);
	}
	if (iss_data.valid_mask & VALID_RSSI) {
		printf("\"RSSI\": %u, ", (unsigned)iss_data.rssi);
	}
	if (iss_data.valid_mask & VALID_RECV_PACKETS) {
		printf("\"recv_packets\": %lu, ", (unsigned long)iss_data.recv_packets);
	}
	if (iss_data.valid_mask & VALID_LOST_PACKETS) {
		printf("\"lost_packets\": %lu, ", (unsigned long)iss_data.lost_packets);
	}
	if (iss_data.valid_mask & VALID_BAD_CRC) {
		printf("\"bad_CRC\": %lu, ", (unsigned long)iss_data.bad_crc);
	}
	if (iss_data.valid_mask & VALID_WIND_SPEED) {
		printf("\"wind_speed_mph\": %u, ", (unsigned)iss_data.wind_speed_mph);
	}
	if (iss_data.valid_mask & VALID_WIND_DIRECTION) {
		printf("\"wind_direction_degrees\": %u, ", (unsigned)iss_data.wind_direction_degrees);
	}
	if (iss_data.valid_mask & VALID_TEMPERATURE) {
		printf("\"temperature_F\": ");
		print_float2(iss_data.temperature_F);
		printf(", ");
	}
	if (iss_data.valid_mask & VALID_HUMIDITY) {
		printf("\"humidity_pct\": ");
		print_float1(iss_data.humidity_pct);
		printf(", ");
	}
	if (iss_data.valid_mask & VALID_LIGHT) {
		printf("\"light\": %u, ", (unsigned)iss_data.light);
	}
	if (iss_data.valid_mask & VALID_UVI) {
		printf("\"UV_index\": ");
		print_float2(iss_data.uv_index);
		printf(", ");
	}
	if (iss_data.valid_mask & VALID_SOLAR) {
		printf("\"solar_Wm2\": ");
		print_float2(iss_data.solar_wm2);
		printf(", ");
	}
	if (iss_data.valid_mask & VALID_RAIN_SPOONS) {
		printf("\"rain_spoons\": %u, ", (unsigned)iss_data.rain_spoons);
	}
	printf("\"raw\": \"");
	for (i=0; i<10; i++) {
		print_hex(iss_data.raw[i]);
		if (i != 9) {
			printf(" ");
		}
	}
	printf("\", ");
	printf("\"version\": \"%s\" }\n", ISS_DATA_VERSION);
}

// parse ISS data - see https://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt
static void parse_iss_data(void)
{
	__pdata uint16_t v;
	__pdata int16_t s;

	if (crc16_ccitt(8) != 0) {
		iss_data.bad_crc++;
		iss_data.valid_mask |= VALID_BAD_CRC;
		return;
	}
	seconds_last_packet = seconds_since_boot;

	iss_data.recv_packets++;
	iss_data.valid_mask |= VALID_RECV_PACKETS;

	iss_data.rssi = radio_last_rssi();
	iss_data.valid_mask |= VALID_RSSI;

	memcpy(iss_data.raw, pbuf, 10);

	iss_data.transmitter_id = pbuf[0]&0x7;
	iss_data.valid_mask |= VALID_TRANSMITTER;

	iss_data.wind_speed_mph = pbuf[1];
	iss_data.valid_mask |= VALID_WIND_SPEED;

	if (pbuf[2] != 0) {
		iss_data.wind_direction_degrees = 0.5 + (pbuf[2] * 360.0 / 255);
		iss_data.valid_mask |= VALID_WIND_DIRECTION;
	}

	// parse packet types
	switch (pbuf[0] & 0xF0) {
	case 0x80: {
		// temperature
		s = (pbuf[3]<<8) | pbuf[4];
		iss_data.temperature_F = 0.5 + (s / 160.0);
		iss_data.valid_mask |= VALID_TEMPERATURE;
		break;
	}

	case 0x70: {
		// light
		v = (pbuf[3]<<4) | (pbuf[4]>>4);
		iss_data.light = v;
		iss_data.valid_mask |= VALID_LIGHT;
		break;
	}

	case 0xA0: {
		// humidity, 
		v = pbuf[3] | ((pbuf[4]>>4)<<8);
		iss_data.humidity_pct = v * 0.1;
		iss_data.valid_mask |= VALID_HUMIDITY;
		break;
	}

	case 0xE0: {
		// rain spoons, 7 bits only
		v = pbuf[3] & 0x7F;
		iss_data.rain_spoons = v;
		iss_data.valid_mask |= VALID_RAIN_SPOONS;
		break;
	}

	case 0x40: {
		// UV index. See http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
		v = (pbuf[3]<<4) | (pbuf[4]>>4);
		iss_data.uv_index = (v-4) / 200.0;
		iss_data.valid_mask |= VALID_UVI;
		break;
	}

	case 0x60: {
		// solar. See http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
		v = (pbuf[3]<<4) | (pbuf[4]>>4);
		iss_data.solar_wm2 = (v-4) / 2.27;
		iss_data.valid_mask |= VALID_SOLAR;
		break;
	}
		
	}

	show_iss_data();
}

/// main loop for time division multiplexing transparent serial
///
void
serial_loop(void)
{
	__pdata uint16_t last_t = timer2_tick();
	__pdata uint16_t last_link_update = last_t;

	_canary = 42;

	// set right receive channel
	radio_set_frequency(fhop_receive_freqency());

	delay_set_ticks(100);

	for (;;) {
		__pdata uint8_t	len;

		if (delay_expired()) {
			delay_set_ticks(100);
			one_second();
		}

		if (_canary != 42) {
			panic("stack blown\n");
		}

		if (pdata_canary != 0x41) {
			panic("pdata canary changed\n");
		}

		// give the AT command processor a chance to handle a command
		at_command();

		// see if we have received a packet
		if (radio_receive_packet(&len, pbuf)) {
			// only look at 10 byte packets
			if (len == 10) {
				swap_packet_bit_order(len);
				fhop_next();
				parse_iss_data();
				delay_set_ticks(100);
				one_second_counter = 0;
			}

			// re-enable the receiver
			radio_set_frequency(fhop_receive_freqency());
			radio_receiver_on();
		}
	}
}


