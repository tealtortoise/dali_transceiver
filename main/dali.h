//
// Created by thea on 5/29/24.
//

#include <stdint.h>
#include <stdio.h>
#ifndef DALI_H
#define DALI_H


#define RMT_RECEIVE_BUFFER_SIZE_SYMBOLS 48



typedef struct {
    uint8_t firstbyte;
    uint8_t secondbyte;
} dali_forward_frame_t;


typedef struct {
    // const rmt_rx_done_event_data_t *event_data;
    rmt_symbol_word_t received_symbols[RMT_RECEIVE_BUFFER_SIZE_SYMBOLS];
    size_t num_symbols;
    // dali_forward_frame_t outgoing;
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

ESP_EVENT_DECLARE_BASE(LIGHTING_EVENT);

enum {
    NEW_LEVEL
}

#endif //DALI_H
