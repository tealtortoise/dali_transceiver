#include "dali_transceiver.h"

static const char *TAG = "DALI transceiver";


dali_transceiver_config_t dali_transceiver_sensible_default_config = {
    .invert_input = DALI_DONT_INVERT,
    .invert_output = DALI_DONT_INVERT,
    .transmit_queue_size_frames = 20,
    .receive_queue_size_frames = 20,
    .parser_config = {
        .backward_frame_action = DALI_PARSER_ACTION_LOG_AND_RECORD,
        .forward_frame_action = DALI_PARSER_ACTION_LOG,
        .mangled_frame_action = DALI_PARSER_ACTION_LOG_AND_RECORD,
    },
};

esp_err_t dali_setup_transceiver(dali_transceiver_config_t config, dali_transceiver_handle_t *handle){
    dali_transmitter_handle_t transmitter;

    esp_err_t returnvalue = setup_dali_transmitter(
        config.transmit_gpio_pin,
        config.invert_output,
        config.transmit_queue_size_frames,
        &transmitter);
    if (returnvalue != ESP_OK) return returnvalue;

    dali_transceiver_t *transceiver = malloc(sizeof(dali_transceiver_t));
    transceiver->transmitter = transmitter;

    QueueHandle_t edgeframe_queue = start_edgelogger(config.receive_gpio_pin, config.invert_input);

    QueueHandle_t dali_received_frame_queue = start_dali_parser(edgeframe_queue, config.parser_config);
    transceiver->dali_received_frame_queue = dali_received_frame_queue;

    *handle = transceiver;
    return ESP_OK;
}

uint32_t dali_transmit_frame(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte ,int queuefull_timeout){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    
    int32_t frameid = transceiver->transmitter.frameidcounter;
    transceiver->transmitter.frameidcounter = frameid + 2;

    dali_frame_t frame = {
        .firstbyte = firstbyte,
        .secondbyte = secondbyte
    };
    dali_transmit_job job = {
        .frame = frame,
        .frameid = frameid,
        .notify_task = xTaskGetCurrentTaskHandle(),
    };
    // ESP_LOGI(TAG, "Sending %d %d", firstbyte, secondbyte);
    if (xQueueSendToBack(transceiver->transmitter.queue, &job, queuefull_timeout)) {
        // ESP_LOGI(TAG, "Transmit queue size %i", uxQueueMessagesWaiting(transceiver->transmitter.queue));
        return frameid;
    }
    return pdFAIL;
}

static uint32_t frameidcount;

BaseType_t dali_transmit_frame_and_wait(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte, TickType_t ticks_to_wait){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    uint32_t frameid = dali_transmit_frame(handle, firstbyte, secondbyte, ticks_to_wait);
    if (!frameid) {
        ESP_LOGE(TAG, "Transmit queue send returned failure (%lu)", frameid);
        return frameid;
    }

    uint32_t value;
    BaseType_t received;
    received = xTaskNotifyWaitIndexed(DALI_NOTIFY_COMPLETE_INDEX, 0, 0, &value, ticks_to_wait);
    if (received && value == frameid) {
        // ESP_LOGI(TAG, "Send completed");
        return pdTRUE;
    }
    ESP_LOGE(TAG, "No send confirmation! (%d, %d)", firstbyte, secondbyte);
    return pdFAIL;
};

dali_frame_t dali_transmit_frame_and_wait_for_backward_frame(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte, TickType_t ticks_to_wait){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    BaseType_t success = dali_transmit_frame_and_wait(handle, firstbyte, secondbyte, ticks_to_wait);
    if (!success) return (dali_frame_t) {
        .firstbyte = 0,
        .secondbyte = 0,
        .type = DALI_TRANSMIT_ERROR
    };

    BaseType_t received;
    dali_frame_t _frame;
    for (int wait=0; wait < ticks_to_wait; wait++){
        received = xQueueReceive(transceiver->dali_received_frame_queue, &_frame, 1);
        if (received && _frame.type != DALI_FORWARD_FRAME_TYPE) {
                // dali_log_frame_prefix(_frame, "Received back:");
                return _frame;
        }
    }
    // ESP_LOGI(TTAG, "No return!");
    return DALI_NO_FRAME;
};

UBaseType_t dali_get_received_frames_in_queue(dali_transceiver_handle_t handle){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    return uxQueueMessagesWaiting(transceiver->dali_received_frame_queue);
};

BaseType_t dali_flush_receive_queue(dali_transceiver_handle_t handle){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    return xQueueReset(transceiver->dali_received_frame_queue);
};

esp_err_t dali_send_twice(dali_transceiver_handle_t handle, uint8_t firstbyte, uint8_t secondbyte){
    BaseType_t received = dali_transmit_frame_and_wait(handle, firstbyte, secondbyte, pdMS_TO_TICKS(100));
    received = received & dali_transmit_frame_and_wait(handle, firstbyte, secondbyte, pdMS_TO_TICKS(100));
    if (received != pdTRUE){
        ESP_LOGE(TAG, "Send error! ( %d, %d )", firstbyte, secondbyte);
        return ESP_ERR_NOT_FINISHED;
    }
    return ESP_OK;
}

esp_err_t dali_set_level_block(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t level){
    BaseType_t success = dali_transmit_frame_and_wait(handle, get_dali_address_byte_setlevel(short_address), level, pdMS_TO_TICKS(100));
    if (!success) return ESP_ERR_NOT_FINISHED;
    return ESP_OK;
}

esp_err_t dali_set_level_noblock(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t level, int queuefull_timeout){
    int frame = dali_transmit_frame(handle, get_dali_address_byte_setlevel(short_address), level, queuefull_timeout);
    if (frame == pdFAIL) return ESP_ERR_NOT_FINISHED;
    return ESP_OK;
}

int16_t dali_query_level(dali_transceiver_handle_t handle, uint8_t short_address){
    // returns -1 in case of no response
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_QUERY_ACTUAL_LEVEL, pdMS_TO_TICKS(1000));
    if (frame.type == DALI_BACKWARD_FRAME_TYPE){
        return (int16_t) frame.firstbyte;
    }
    ESP_LOGE(TAG, "No response to level query of address %d", short_address);
    return -1;
}
int16_t dali_query_dtr(dali_transceiver_handle_t handle, uint8_t short_address){
    // returns -1 in case of no response
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_QUERY_DTR, pdMS_TO_TICKS(1000));
    if (frame.type == DALI_BACKWARD_FRAME_TYPE){
        return (int16_t) frame.firstbyte;
    }
    ESP_LOGE(TAG, "No response to query DTR %d", short_address);
    return -1;
}

esp_err_t dali_broadcast_level(dali_transceiver_handle_t handle, uint8_t level){
    BaseType_t success = dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_BROADCAST_LEVEL, level, pdMS_TO_TICKS(100));
    if (!success) return ESP_ERR_NOT_FINISHED;
    return ESP_OK;
}

esp_err_t dali_set_and_verify_dtr(dali_transceiver_handle_t handle, uint8_t broadcast_value, uint8_t short_address_verify){
    BaseType_t success = dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_SET_DTR, broadcast_value, pdMS_TO_TICKS(100));
    if (!success) return ESP_ERR_NOT_FINISHED;
    int16_t query = dali_query_dtr(handle, short_address_verify);
    if (query != broadcast_value) {
        ESP_LOGE(TAG, "DTR verification failed %i != %d", query, broadcast_value);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t dali_configure_and_verify(dali_transceiver_handle_t handle, uint8_t firstbyte_config, uint8_t secondbyte_config, uint8_t firstbyte_verify, uint8_t secondbyte_verify, uint8_t correct_response_byte){
    if (dali_send_twice(handle, firstbyte_config, secondbyte_config)) {
        return ESP_ERR_NOT_FINISHED;
    }
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, firstbyte_verify, secondbyte_verify, pdMS_TO_TICKS(1000));
    if (frame.type == DALI_NO_FRAME_TYPE){
        ESP_LOGE(TAG, "No response from control gear");
        return ESP_ERR_NOT_FOUND;
    }
    if (frame.type == DALI_MANGLED_FRAME){
        ESP_LOGE(TAG, "Mangled response from control gear");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (frame.firstbyte != correct_response_byte) {
        ESP_LOGE(TAG, "Query did not give expected response %d != %d", frame.firstbyte, correct_response_byte);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Success");
    return ESP_OK;
}

esp_err_t dali_set_fade_time(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t fade_time){
    ESP_LOGI(TAG, "Setting fade time on %d to %d", short_address, fade_time);
    if (fade_time > 15) {
        ESP_LOGE(TAG, "Fade time must be 0 <= time <= 15");
        return ESP_ERR_NOT_ALLOWED;
        }
    if (dali_set_and_verify_dtr(handle, fade_time, short_address)) {
        return ESP_ERR_NOT_FINISHED;
    };
    if (dali_send_twice(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_STORE_DTR_AS_FADE_TIME)) {
        return ESP_ERR_NOT_FINISHED;
    };
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_QUERY_FADE_RATE, pdMS_TO_TICKS(1000));
    if (frame.type == DALI_NO_FRAME_TYPE){
        ESP_LOGE(TAG, "No response to fade time query from control gear");
        return ESP_ERR_NOT_FOUND;
    }
    if (frame.type == DALI_MANGLED_FRAME){
        ESP_LOGE(TAG, "Mangled response to fade time query from control gear");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if ((frame.firstbyte >> 4) != fade_time) {
        ESP_LOGE(TAG, "Query did not give expected response %d != %d", (frame.firstbyte >> 4), fade_time);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Success - fade time on %d now %d", short_address, fade_time);
    return ESP_OK;
}

esp_err_t dali_set_power_on_level(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t power_on_level){
    ESP_LOGI(TAG, "Setting pwer on level on %d to %d", short_address, power_on_level);

    if (dali_set_and_verify_dtr(handle, power_on_level, short_address)) {
        return ESP_ERR_NOT_FINISHED;
    };
    if (dali_send_twice(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_STORE_DTR_AS_POWER_ON_LEVEL)) {
        return ESP_ERR_NOT_FINISHED;
    };
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_QUERY_POWER_ON_LEVEL, pdMS_TO_TICKS(1000));
    if (frame.type == DALI_NO_FRAME_TYPE){
        ESP_LOGE(TAG, "No response to power on level query from control gear");
        return ESP_ERR_NOT_FOUND;
    }
    if (frame.type == DALI_MANGLED_FRAME){
        ESP_LOGE(TAG, "Mangled response to power on level query from control gear");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (frame.firstbyte  != power_on_level) {
        ESP_LOGE(TAG, "Query did not give expected response %d != %d", frame.firstbyte, power_on_level);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Success - power on level on %d now %d", short_address, power_on_level);
    return ESP_OK;
}
esp_err_t dali_set_system_failure_level(dali_transceiver_handle_t handle, uint8_t short_address, uint8_t system_failure_level){
    ESP_LOGI(TAG, "Setting system failure level on %d to %d", short_address, system_failure_level);

    if (dali_set_and_verify_dtr(handle, system_failure_level, short_address)) {
        return ESP_ERR_NOT_FINISHED;
    };
    if (dali_send_twice(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_STORE_DTR_AS_SYSTEM_FAILURE_LEVEL)) {
        return ESP_ERR_NOT_FINISHED;
    };
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, get_dali_address_byte(short_address), DALI_SECONDBYTE_QUERY_SYSTEM_FAILURE_LEVEL, pdMS_TO_TICKS(1000));
    if (frame.type == DALI_NO_FRAME_TYPE){
        ESP_LOGE(TAG, "No response to system failure level query from control gear");
        return ESP_ERR_NOT_FOUND;
    }
    if (frame.type == DALI_MANGLED_FRAME){
        ESP_LOGE(TAG, "Mangled response to system failure level query from control gear");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (frame.firstbyte  != system_failure_level) {
        ESP_LOGE(TAG, "Query did not give expected response %d != %d", frame.firstbyte, system_failure_level);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Success - system failure level on %d now %d", short_address, system_failure_level);
    return ESP_OK;
}