

#ifndef DALI_H
#define DALI_H
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// #include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
// #include "esp_event_base.h"

#define RMT_RECEIVE_BUFFER_SIZE_SYMBOLS 48

typedef struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    int level;
} httpd_ctx;

#define DALI_FORWARD_FRAME_TYPE 0
#define DALI_BACKWARD_FRAME_TYPE 1
#define DALI_MANGLED_FRAME 2
#define DALI_NO_FRAME_TYPE 3
#define DALI_TRANSMIT_ERROR 3
#define DALI_RECIEVE_ERROR 4

typedef struct {
    uint8_t firstbyte;
    uint8_t secondbyte;
    uint8_t type;
} dali_frame_t;

static const dali_frame_t DALI_NO_FRAME = {
    .firstbyte = 0,
    .secondbyte = 0,
    .type = DALI_NO_FRAME_TYPE
};

typedef struct {
    dali_frame_t frame;
    TaskHandle_t notify_task;
    int32_t frameid;
} dali_transmit_job;

typedef struct {
    // const rmt_rx_done_event_data_t *event_data;
    rmt_symbol_word_t received_symbols[RMT_RECEIVE_BUFFER_SIZE_SYMBOLS];
    size_t num_symbols;
    // dali_frame_t outgoing;
    // uint32_t num;
} dali_rmt_received_frame_t;

typedef struct {
    uint16_t time;
    int8_t edgetype;
} edge_t;

typedef struct {
    edge_t edges[64];
    uint8_t length;
}
edgeframe;


#define EDGETYPE_RISING 1
#define EDGETYPE_FALLING 0
#define EDGETYPE_NONE -1

#define DALI_FIRSTBYTE_BROADCAST_LEVEL 0b11111110
#define DALI_FIRSTBYTE_BROADCAST_COMMAND 0b11111111
#define DALI_FIRSTBYTE_INITALISE 0xA5
#define DALI_FIRSTBYTE_TERMINATE 0b10100001
#define DALI_FIRSTBYTE_RANDOMIZE 0xA7
#define DALI_FIRSTBYTE_SEARCH_HIGH 0b10110001
#define DALI_FIRSTBYTE_SEARCH_MID 0b10110011
#define DALI_FIRSTBYTE_SEARCH_LOW 0b10110101
#define DALI_FIRSTBYTE_COMPARE 0b10101001
#define DALI_FIRSTBYTE_PROGRAM_SHORT_ADDRESS 0b10110111
#define DALI_FIRSTBYTE_QUERY_SHORT_ADDRESS 0b10111011
#define DALI_FIRSTBYTE_VERIFY_SHORT_ADDRESS 0b10111001
#define DALI_FIRSTBYTE_SET_DTR 0b10100011
#define DALI_FIRSTBYTE_WITHDRAW 0b10101011


#define DALI_SECONDBYTE_COMMAND_RESET 0x20
#define DALI_SECONDBYTE_COMMAND_OFF 0x00
#define DALI_SECONDBYTE_TERMINATE 0x00

#define DALI_SECONDBYTE_RESET_ALL 0x20
#define DALI_SECONDBYTE_QUERY_DTR 0b10011000
#define DALI_SECONDBYTE_STORE_DTR_AS_SHORT_ADDRESS 0b10000000
#define DALI_SECONDBYTE_STORE_DTR_AS_FADE_TIME 0b00101110
#define DALI_SECONDBYTE_STORE_DTR_AS_POWER_ON_LEVEL 0b00101101
#define DALI_SECONDBYTE_QUERY_MISSING_SHORT_ADDRESS 150
#define DALI_SECONDBYTE_INITIALISE_ALL_WITHOUT_SHORT_ADDRESS 0xFF
#define DALI_SECONDBYTE_INITIALISE_ALL 0x00
#define DALI_SECONDBYTE_RANDOMIZE 0
#define DALI_SECONDBYTE_QUERY_SHORT_ADDRESS 0
#define DALI_SECONDBYTE_QUERY_ACTUAL_LEVEL 0b10100000
#define DALI_SECONDBYTE_QUERY_FADE_RATE 0b10100101
// #define DALI_SECONDBYTE_VERIFY_SHORT_ADDRESS 0
#define DALI_SECONDBYTE_COMPARE 0
#define DALI_SECONDBYTE_WITHDRAW 0

uint8_t get_dali_address_byte(uint8_t address);

uint8_t get_dali_address_byte_setlevel(uint8_t address);

void dali_log_frame(dali_frame_t frame);

void log_dali_frame_prefix(dali_frame_t frame, char *prefix);

#pragma once

#endif 

/*
// Type of Addresses Byte Description
// Short address 0AAAAAAS (AAAAAA = 0 to 63, S = 0/1)
// Group address 100AAAAS (AAAA = 0 to 15, S = 0/1)
// Broadcast address 1111111S (S = 0/1)
// Special command 101CCCC1 (CCCC = command number)


// Indirect DALI commands for lamp power
// -------------------------------------

// These commands take the form YAAA AAA1 xxXXxx.

// xxXXxx: These 8 bits transfer the command number. The available command numbers are listed and explained in the following tables in hexadecimal and decimal formats.

// 0 YAAA AAA1 0000 0000 OFF
// 1 YAAA AAA1 0000 0001 UP
// 2 YAAA AAA1 0000 0010 DOWN
// 3 YAAA AAA1 0000 0011 STEP UP
// 4 YAAA AAA1 0000 0100 STEP DOWN
// 5 YAAA AAA1 0000 0101 RECALL MAX LEVEL
// 6 YAAA AAA1 0000 0110 RECALL MIN LEVEL
// 7 YAAA AAA1 0000 0111 STEP DOWN AND OFF
// 8 YAAA AAA1 0000 1000 ON AND STEP UP
// 9 YAAA AAA1 0000 1001 ENABLE DAPC SEQUENCE

// 16 - 31 YAAA AAA1 0001 XXXX GO TO SCENE
// 32 YAAA AAA1 0010 0000 RESET
// 33 YAAA AAA1 0010 0001 STORE ACTUAL LEVEL IN THE DTR

// 42 YAAA AAA1 0010 1010 STORE THE DTR AS MAX LEVEL
// 43 YAAA AAA1 0010 1011 STORE THE DTR AS MIN LEVEL
// 44 YAAA AAA1 0010 1100 STORE THE DTR AS SYSTEM FAILURE LEVEL
// 45 YAAA AAA1 0010 1101 STORE THE DTR AS POWER ON LEVEL
// 46 YAAA AAA1 0010 1110 STORE THE DTR AS FADE TIME
// 47 YAAA AAA1 0010 1111 STORE THE DTR AS FADE RATE

// 64 - 79 YAAA AAA1 0100 XXXX STORE THE DTR AS SCENE
// 80 - 95 YAAA AAA1 0101 XXXX REMOVE FROM SCENE
// 96 - 111 YAAA AAA1 0110 XXXX ADD TO GROUP
// 112 - 127 YAAA AAA1 0111 XXXX REMOVE FROM GROUP
// 128 YAAA AAA1 1000 0000 STORE DTR AS SHORT ADDRESS
// 129 YAAA AAA1 1000 0001 ENABLE WRITE MEMORY

// 144 YAAA AAA1 1001 0000 QUERY STATUS
// 145 YAAA AAA1 1001 0001 QUERY CONTROL GEAR
// 146 YAAA AAA1 1001 0010 QUERY LAMP FAILURE
// 147 YAAA AAA1 1001 0011 QUERY LAMP POWER ONLicensed by SOURCE to METROLIGHT

// 148 YAAA AAA1 1001 0100 QUERY LIMIT ERROR
// 149 YAAA AAA1 1001 0101 QUERY RESET STATE
// 150 YAAA AAA1 1001 0110 QUERY MISSING SHORT ADDRESS
// 151 YAAA AAA1 1001 0111 QUERY VERSION NUMBER
// 152 YAAA AAA1 1001 1000 QUERY CONTENT DTR
// 153 YAAA AAA1 1001 1001 QUERY DEVICE TYPE
// 154 YAAA AAA1 1001 1010 QUERY PHYSICAL MINIMUM LEVEL
// 155 YAAA AAA1 1001 1011 QUERY POWER FAILURE
// 156 YAAA AAA1 1001 1100 QUERY CONTENT DTR1
// 157 YAAA AAA1 1001 1101 QUERY CONTENT DTR2

// 160 YAAA AAA1 1010 0000 QUERY ACTUAL LEVEL
// 161 YAAA AAA1 1010 0001 QUERY MAX LEVEL
// 162 YAAA AAA1 1010 0010 QUERY MIN LEVEL
// 163 YAAA AAA1 1010 0011 QUERY POWER ON LEVEL
// 164 YAAA AAA1 1010 0100 QUERY SYSTEM FAILURE LEVEL
// 165 YAAA AAA1 1010 0101 QUERY FADE TIME/FADE RATE

// 176 - 191 YAAA AAA1 1011 XXXX QUERY SCENE LEVEL (SCENES 0-15)
// 192 YAAA AAA1 1100 0000 QUERY GROUPS 0-7
// 193 YAAA AAA1 1100 0001 QUERY GROUPS 8-15
// 194 YAAA AAA1 1100 0010 QUERY RANDOM ADDRESS (H)
// 195 YAAA AAA1 1100 0011 QUERY RANDOM ADDRESS (M)
// 196 YAAA AAA1 1100 0100 QUERY RANDOM ADDRESS (L)
// 197 YAAA AAA1 1100 0101 READ MEMORY LOCATION

// 224 - 254 YAAA AAA1 111X XXXX See parts 2XX of this standard

// 255 YAAA AAA1 1111 1111 QUERY EXTENDED VERSION NUMBER
// 256 1010 0001 0000 0000 TERMINATE
// 257 1010 0011 XXXX XXXX DATA TRANSFER REGISTER (DTR)
// 258 1010 0101 XXXX XXXX INITIALISE
// 259 1010 0111 0000 0000 RANDOMISE
// 260 1010 1001 0000 0000 COMPARE
// 261 1010 1011 0000 0000 WITHDRAW
// 262 - 263 1010 11X1 0000 0000 a
// 264 1011 0001 HHHH HHHH SEARCHADDRH
// 265 1011 0011 MMMM MMMM SEARCHADDRM
// 266 1011 0101 LLLL LLLL SEARCHADDRL

// 267 1011 0111 0AAA AAA1 PROGRAM SHORT ADDRESS
// 268 1011 1001 0AAA AAA1 VERIFY SHORT ADDRESS
// 269 1011 1011 0000 0000 QUERY SHORT ADDRESS
// 270 1011 1101 0000 0000 PHYSICAL SELECTION

// 272 1100 0001 XXXX XXXX ENABLE DEVICE TYPE X
// 273 1100 0011 XXXX XXXX DATA TRANSFER REGISTER 1 (DTR1)
// 274 1100 0101 XXXX XXXX DATA TRANSFER REGISTER 2 (DTR2)
// 275 1100 0111 XXXX XXXX WRITE MEMORY LOCATION

// These Extended commands have not address field. Each device has to be configured before the installation.

// Extended command (224-254) for type-6 devices (standard 207):
// 224 YAAA AAA1 1110 0000 REFERENCE SYSTEM POWER
// 225 YAAA AAA1 1110 0001 ENABLE CURRENT PROTECTOR
// 226 YAAA AAA1 1110 0010 DISABLE CURRENT PROTECTOR
// 227 YAAA AAA1 1110 0011 SELECT DIMMING CURVE
// 228 YAAA AAA1 1110 0100 STORE DTR AS FAST FADE TIME

// 237 YAAA AAA1 1110 1101 QUERY GEAR TYPE
// 238 YAAA AAA1 1110 1110 QUERY DIMMING CURVE
// 239 YAAA AAA1 1110 1111 QUERY POSSIBLE OPERATING MODES
// 240 YAAA AAA1 1111 0000 QUERY FEATURES
// 241 YAAA AAA1 1111 0001 QUERY FAILURE STATUS
// 242 YAAA AAA1 1111 0010 QUERY SHORT CIRCUIT
// 243 YAAA AAA1 1111 0011 QUERY OPEN CIRCUIT
// 244 YAAA AAA1 1111 0100 QUERY LOAD DECREASE
// 245 YAAA AAA1 1111 0101 QUERY LOAD INCREASE
// 246 YAAA AAA1 1111 0110 QUERY CURRENT PROTECTOR ACTIVE
// 247 YAAA AAA1 1111 0111 QUERY THERMAL SHUT DOWN
// 248 YAAA AAA1 1111 1000 QUERY THERMAL OVERLOAD
// 249 YAAA AAA1 1111 1001 QUERY REFERENCE RUNNING
// 250 YAAA AAA1 1111 1010 QUERY REFERENCE MEASUREMENT FAILED
// 251 YAAA AAA1 1111 1011 QUERY CURRENT PROTECTOR ENABLED
// 252 YAAA AAA1 1111 1100 QUERY OPERATING MODE
// 253 YAAA AAA1 1111 1101 QUERY FAST FADE TIME
// 254 YAAA AAA1 1111 1110 QUERY MIN FAST FADE TIME
// 255 YAAA AAA1 1111 1111 QUERY EXTENDED VERSION NUMBER
// 272 1100 0001 0000 0110 ENABLE DEVICE TYPE 6

// Note Repeat of DALI commands 
// ----------------------------

// According to IEC 60929, a DALI Master has to repeat several commands within 100 ms, so that DALI-Slaves will execute them. 

// The DALI Master Terminal KL6811 repeats the commands 32dez to 128dez, 258dez and 259dez (bold marked) automatically to make the the double call from the user program unnecessary.

// The DALI Master Terminal KL6811 repeats also the commands 224dez to 255dez, if you have activated this with Bit 1 of the Control-Byte (CB.1) before.

// DALI Control Device Type List
// -----------------------------

// Type DEC Type HEX Name Comments
// 128 0x80 Unknown Device. If one of the devices below don�t apply
// 129 0x81 Switch Device A Wall-Switch based Controller including, but not limited to ON/OFF devices, Scene switches, dimming device.
// 130 0x82 Slide Dimmer An analog/positional dimming controller 
// 131 0x83 Motion/Occupancy Sensor. A device that indicates the presence of people within a control area.
// 132 0x84 Open-loop daylight Controller. A device that outputs current light level and/or sends control messages to actuators based on light passing a threshold.
// 133 0x85 Closed-loop daylight controller. A device that outputs current light level and/or sends control messages to actuators based on a change in light level.
// 134 0x86 Scheduler. A device that establishes the building mode based on time of day, or which provides control outputs.
// 135 0x87 Gateway. An interface to other control systems or communication busses
// 136 0x88 Sequencer. A device which sequences lights based on a triggering event
// 137 0x89 Power Supply *). A DALI Power Supply device which supplies power for the communication loop
// 138 0x8a Emergency Lighting Controller. A device, which is certified for use in control of emergency lighting, or, if not certified, for noncritical backup lighting.
// 139 0x8b Analog input unit. A general device with analog input.
// 140 0x8c Data Logger. A unit logging data (can be digital or analog data)
*/