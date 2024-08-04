#ifndef DALI_TC_H
#define DALI_TC_H

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "dali.h"
#include "dali_transmit.h"
#include "dali_edgeframe_parser.h"
#include "edgeframe_logger.h"
// #import 


typedef struct {
    uint8_t receive_gpio_pin;
    uint8_t transmit_gpio_pin;
    uint8_t invert_input;
    uint8_t invert_output;
    uint8_t transmit_queue_size_frames;
    uint8_t receive_queue_size_frames;
    dali_parser_config_t parser_config;
} dali_transceiver_config_t;

extern dali_transceiver_config_t dali_transceiver_sensible_default_config;

esp_err_t dali_setup_transceiver(dali_transceiver_config_t config, dali_transceiver_handle_t *handle);

bool start_receiver(dali_transceiver_handle_t handle, bool wait);

bool suspend_main_task(dali_transceiver_handle_t handle);

void resume_main_task(dali_transceiver_handle_t handle);

bool dali_take_mutex(dali_transceiver_handle_t handle, TickType_t wait_ticks);

void dali_give_mutex(dali_transceiver_handle_t handle);

bool stop_receiver_and_clear_queues(dali_transceiver_handle_t handle);

uint32_t dali_transmit_frame(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte, int queuefull_timeout);


BaseType_t dali_transmit_frame_and_wait(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte, TickType_t ticks_to_wait);

dali_frame_t dali_transmit_frame_and_wait_for_backward_frame(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte, TickType_t ticks_to_wait);

UBaseType_t dali_get_received_frames_in_queue(dali_transceiver_handle_t handle);

BaseType_t dali_flush_receive_queue(dali_transceiver_handle_t handle);

esp_err_t dali_set_level_block(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t level);

esp_err_t dali_set_level_noblock(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t level, int queuefull_timeout);

esp_err_t dali_set_level_group_noblock(dali_transceiver_handle_t handle, uint8_t group, uint8_t level, int queuefull_timeout);

int16_t dali_query_level(dali_transceiver_handle_t handle, uint8_t short_address);

esp_err_t dali_broadcast_level_noblock(dali_transceiver_handle_t handle, uint8_t level);

esp_err_t dali_set_and_verify_dtr(dali_transceiver_handle_t handle, uint8_t broadcast_value, uint8_t short_address_verify);

esp_err_t dali_set_fade_time(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t fade_time);

esp_err_t dali_set_power_on_level(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t power_on_level);

esp_err_t dali_set_system_failure_level(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t system_failure_level);

#pragma once
#endif
