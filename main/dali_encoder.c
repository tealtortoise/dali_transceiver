/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "dali_encoder.h"

// HALF_BIT_COUNT,
// #define HALF_BIT_TIME_US 0xfff0ULL / 100000 * 1000000 // 833us
#define HALF_BIT_COUNT 4167 // 100 ns units
// #define HALF_BIT_COUNT 0x7ff0 // 100 ns units
// #define HALF_BIT_COUNT_B 0x0a00

static const char *TAG = "dali_encoder";

typedef struct {
    rmt_encoder_t base;           // the base "class", declares the standard encoder interface
    rmt_encoder_t *copy_encoder;  // use the copy_encoder to encode the leading and ending pulse
    rmt_encoder_t *bytes_encoder; // use the bytes_encoder to encode the address and command data
    rmt_symbol_word_t dali_leading_symbol; // dali leading code with RMT representation
    rmt_symbol_word_t dali_ending_symbol;  // dali ending code with RMT representation
    int state;
} rmt_dali_encoder_t;

static size_t rmt_encode_dali(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_dali_encoder_t *dali_encoder = __containerof(encoder, rmt_dali_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    dali_frame_t *scan_code = (dali_frame_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = dali_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = dali_encoder->bytes_encoder;
    switch (dali_encoder->state) {
    case 0: // send leading code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dali_encoder->dali_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            dali_encoder->state = 1; // we can only switch to next state when current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    // fall-through
    case 1: // send first byte
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->firstbyte, sizeof(uint8_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            dali_encoder->state = 2; // we can only switch to next state when current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    // fall-through
    case 2: // send second byte
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, &scan_code->secondbyte, sizeof(uint8_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            dali_encoder->state = 3; // we can only switch to next state when current encoder finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    // fall-through
    case 3: // send ending code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &dali_encoder->dali_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            dali_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space to put other encoding artifacts
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_dali_encoder(rmt_encoder_t *encoder)
{
    rmt_dali_encoder_t *dali_encoder = __containerof(encoder, rmt_dali_encoder_t, base);
    rmt_del_encoder(dali_encoder->copy_encoder);
    rmt_del_encoder(dali_encoder->bytes_encoder);
    free(dali_encoder);
    return ESP_OK;
}

static esp_err_t rmt_dali_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_dali_encoder_t *dali_encoder = __containerof(encoder, rmt_dali_encoder_t, base);
    rmt_encoder_reset(dali_encoder->copy_encoder);
    rmt_encoder_reset(dali_encoder->bytes_encoder);
    dali_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_dali_encoder(const dali_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_dali_encoder_t *dali_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    dali_encoder = rmt_alloc_encoder_mem(sizeof(rmt_dali_encoder_t));
    ESP_GOTO_ON_FALSE(dali_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for ir dali encoder");
    dali_encoder->base.encode = rmt_encode_dali;
    dali_encoder->base.del = rmt_del_dali_encoder;
    dali_encoder->base.reset = rmt_dali_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &dali_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    // construct the leading code and ending code with RMT symbol format
    dali_encoder->dali_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = HALF_BIT_COUNT,
        .level1 = 0,
        .duration1 = HALF_BIT_COUNT
    };
    dali_encoder->dali_ending_symbol = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = HALF_BIT_COUNT * 2,
        .level1 = 0,
        .duration1 = HALF_BIT_COUNT * 2,
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 0,
            .duration0 = HALF_BIT_COUNT,
            .level1 = 1,
            .duration1 = HALF_BIT_COUNT,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = HALF_BIT_COUNT,
            .level1 = 0,
            .duration1 = HALF_BIT_COUNT,
        },
        .flags = {
            .msb_first = 1,
        },
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &dali_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");

    *ret_encoder = &dali_encoder->base;
    return ESP_OK;
err:
    if (dali_encoder) {
        if (dali_encoder->bytes_encoder) {
            rmt_del_encoder(dali_encoder->bytes_encoder);
        }
        if (dali_encoder->copy_encoder) {
            rmt_del_encoder(dali_encoder->copy_encoder);
        }
        free(dali_encoder);
    }
    return ret;
}
