#include "dali_utils.h"
#include "esp_err.h"
#include "freertos/queue.h"

static const char *TAG = "Dali Utils";

void set_24bit_address(dali_transceiver_handle_t handle, uint32_t address){
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_SEARCH_HIGH, (address >> 16) & 0xff, pdMS_TO_TICKS(1000));
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_SEARCH_MID, (address >> 8) & 0xff, pdMS_TO_TICKS(1000));
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_SEARCH_LOW, (address >> 0) & 0xff, pdMS_TO_TICKS(1000));
}

bool search_below(dali_transceiver_handle_t handle, uint32_t end){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    ESP_LOGI(TAG, "Searching to %#08lx", end);
    set_24bit_address(handle, end);
    dali_frame_t frame = dali_transmit_frame_and_wait_for_backward_frame(handle, DALI_FIRSTBYTE_COMPARE, DALI_SECONDBYTE_COMPARE, pdMS_TO_TICKS(100));
    return (frame.type != DALI_NO_FRAME_TYPE);
}

static uint32_t fake[] = {0x123454, 0xF52332, 0x029876};

bool mock_search_below(dali_transceiver_handle_t handle, uint32_t end){
    for (int i=0; i < 3; i++){
        if (fake[i] <= end) return true;
    }
    return false;
}

void log_fakes(){
    ESP_LOGI(TAG, "Fake addresses:");
    for (int i=0; i < 3; i++){
        if (fake[i] != 0xF0FFFFFF){
            ESP_LOGI(TAG, "Fake address %u: %#08lx", i, fake[i]);
        }
    }
}

void remove_fake(uint32_t address){
    bool done = false;
    for (int i=0; i<3; i++){
        if (fake[i] == address) {
            fake[i] = 0xF0FFFFFF;
            done = true;
        }
    }
    if (done) {
        ESP_LOGI(TAG, "Address %#08lx removed", address);
        log_fakes();
    }
    else
    {
        ESP_LOGE(TAG, "NOPE none found at %#08lx", address);
        log_fakes();
    }

};



esp_err_t _dali_assign_short_addresses(dali_transceiver_handle_t handle, int start_address){
    ESP_LOGI(TAG, "Commissioning started - initialise");
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_INITALISE, DALI_SECONDBYTE_INITIALISE_ALL, pdMS_TO_TICKS(100));
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_INITALISE, DALI_SECONDBYTE_INITIALISE_ALL, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Randomize");
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_RANDOMIZE, DALI_SECONDBYTE_RANDOMIZE, pdMS_TO_TICKS(100));
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_RANDOMIZE, DALI_SECONDBYTE_RANDOMIZE, pdMS_TO_TICKS(100));

    int short_address = start_address;
    bool more = true;
    dali_frame_t frame;
    BaseType_t received;
    while (more) {
        ESP_LOGI(TAG, "Staring search loop");
        uint32_t start= 0;
        uint32_t end = 0xFFFFFF;
        bool cont = true;
        bool present = false;
        bool found = false;
        uint32_t lastend = end;
        while (cont){
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Searching %#08lx - %#08lx", start, end);
            present = search_below(handle, end);
            if (present) {
                if (start == end){
                    // singled out
                    ESP_LOGI(TAG, "Found one! %#08lx", end);
                    // more = false;
                    found = true;
                    break;
                }
                else
                {
                    // present but not narrowed down
                    lastend = end;
                    end = start + (end - start) / 2;
                    ESP_LOGI(TAG, "Address present, moving end down to %#08lx", end);
                }
            }
            else
            {
                // not present
                if (end == 0xFFFFFF){
                    // none exist
                    cont = false;
                    more = false;
                    found = false;
                    ESP_LOGI(TAG, "Nothing found %#08lx", end);
                }
                else
                {
                    // lastend was better, bring start later
                    end = lastend;
                    start = end - (end - start) / 2;
                    ESP_LOGI(TAG, "Not present, moving start to %#08lx and end back to %#08lx", start, end);
                }
            }
        }

        if (found) {
            if (start != end) {
                ESP_LOGE(TAG, "What??? start != end %#08lx %#08lx", start, end);
                return ESP_ERR_INVALID_STATE;
            }
            ESP_LOGI(TAG, "Assigning short address to %#08lx", end);
            ESP_LOGI(TAG, "Programming short address %u", short_address);
            dali_transmit_frame_and_wait(handle, 
                DALI_FIRSTBYTE_PROGRAM_SHORT_ADDRESS, 
                get_dali_command_address_byte(short_address), pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Verify short address...");
            frame = dali_transmit_frame_and_wait_for_backward_frame(handle, DALI_FIRSTBYTE_VERIFY_SHORT_ADDRESS, get_dali_command_address_byte(short_address), pdMS_TO_TICKS(1000));
            if (frame.type != DALI_BACKWARD_FRAME_TYPE)
            {
                ESP_LOGE(TAG, "No response!");
                return ESP_ERR_NOT_FOUND;
            }
            else if (frame.firstbyte != DALI_YES)
            {
                ESP_LOGE(TAG, "Short address not as expected %d != %d", frame.firstbyte, get_dali_command_address_byte(short_address));
                return ESP_ERR_INVALID_RESPONSE;
            }
            ESP_LOGI(TAG, "Sending Withdraw");
            dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_WITHDRAW, DALI_SECONDBYTE_WITHDRAW, pdMS_TO_TICKS(100));
            dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_WITHDRAW, DALI_SECONDBYTE_WITHDRAW, pdMS_TO_TICKS(100));
            short_address += 1;
            ESP_LOGI(TAG, "Next short address will be %i", short_address);
            vTaskDelay(pdMS_TO_TICKS(500));
            // remove_fake(end);

        }
        vTaskDelay(pdMS_TO_TICKS(8000));
    }
    ESP_LOGI(TAG, "End provisioning! Found %i devices", short_address - start_address);
    return ESP_OK;
}

esp_err_t dali_assign_short_addresses(dali_transceiver_handle_t handle, int start_address){
    bool oldstate = start_receiver(handle, true);
    // dali_take_mutex(handle, pdMS_TO_TICKS(5000));
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    // bool oldstate = transceiver->
    esp_err_t response = _dali_assign_short_addresses(handle, start_address);
    if (response != ESP_OK){
        ESP_LOGE(TAG, "Commissioning failed");
    }
    ESP_LOGI(TAG, "Sending TERMINATE");
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_TERMINATE, DALI_SECONDBYTE_TERMINATE, pdMS_TO_TICKS(100));
    dali_transmit_frame_and_wait(handle, DALI_FIRSTBYTE_TERMINATE, DALI_SECONDBYTE_TERMINATE, pdMS_TO_TICKS(100));
    if (!oldstate) {
        stop_receiver_and_clear_queues(handle);
    }
    // dali_give_mutex(handle);
    return response;
}


void dali_command_monitor_task(void* params){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) params;
    BaseType_t received;
    dali_command_t command;
    esp_err_t err;
    while (1)
    {
        if (xQueueReceive(transceiver->dali_command_queue, &command, portMAX_DELAY))
        {   
            ESP_LOGI(TAG, "Notify task %i", (int) command.notify_task);
            dali_take_mutex((dali_transceiver_handle_t) transceiver, pdMS_TO_TICKS(5000));
            switch (command.command)
            {
            case DALI_COMMAND_COMMISSION:
                ESP_LOGI(TAG, "Received COMMISSION command...");
                vTaskSuspend(transceiver->mainloop_task);
                err = dali_assign_short_addresses(transceiver, command.address);
                vTaskResume(transceiver->mainloop_task);
                if (err) ESP_LOGE(TAG, "Commissioning returned error %i", err);
                xTaskNotifyIndexed(command.notify_task, DALI_COMMAND_RETURN_INDEX, err, eSetValueWithOverwrite);
                break;
            case DALI_COMMAND_SET_POWER_ON_LEVEL:
                ESP_LOGI(TAG, "Received SET_POWER_ON_LEVEL command...");
                err = dali_set_power_on_level(transceiver, command.address, command.value);
                xTaskNotifyIndexed(command.notify_task, DALI_COMMAND_RETURN_INDEX, err, eSetValueWithOverwrite);
                
                break;
            case DALI_COMMAND_SET_FAILSAFE_LEVEL:
                ESP_LOGI(TAG, "Received SET_FAILSAFE_LEVEL command...");
                err = dali_set_system_failure_level(transceiver, command.address, command.value);
                xTaskNotifyIndexed(command.notify_task, DALI_COMMAND_RETURN_INDEX, err, eSetValueWithOverwrite);
                
                break;
            default:
                ESP_LOGE(TAG, "Unknown command %i", command.command);
                xTaskNotifyIndexed(command.notify_task, DALI_COMMAND_RETURN_INDEX, ESP_ERR_INVALID_ARG, eSetValueWithOverwrite);
                break;
            }
            
            dali_give_mutex((dali_transceiver_handle_t) transceiver);
        }
    }
}

QueueHandle_t dali_setup_command_queue(dali_transceiver_handle_t handle){
    dali_transceiver_t *transceiver = (dali_transceiver_t *) handle;
    QueueHandle_t commandqueue = xQueueCreate(1, sizeof(dali_command_t));
    TaskHandle_t cmdtask;
    xTaskCreate(dali_command_monitor_task, "dali-cmd-monitor", 8192, (void*) transceiver, 1, &cmdtask);
    transceiver->dali_command_queue = commandqueue;
    return commandqueue;
}


    // while (1) {
    //     // ESP_LOGI(TAG, "Set dtr0 %d", 49);
    //     // set_dtr0.secondbyte = (49 << 1) + 1;
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_dtr0, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // ESP_LOGI(TAG, "Query dtr");
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &query_dtr, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // ESP_LOGI(TAG, "set dtr0 as short address twice %d", 49);
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &storedtr0asshortaddress, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(20));
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &storedtr0asshortaddress, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // ESP_LOGI(TAG, "Query dtr 49");
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &query_dtr_49, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //     // set_level_template.secondbyte = 10;
    //     // ESP_LOGI(TAG, "Set level %d", 10);
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     set_level_template.firstbyte = (49 << 1) + 1;
    //     set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //     set_level_template.secondbyte = 100;
    //     ESP_LOGI(TAG, "Set address all to level %d", 100);
    //     ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //     vTaskDelay(pdMS_TO_TICKS(300));
    //
    //     for (int i = 0; i< 64; i++) {
    //
    //         set_level_template.firstbyte = (i << 1);
    //         // set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //         set_level_template.secondbyte = 1;
    //         ESP_LOGI(TAG, "Set address %d to level 1", i);
    //         ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(300));
    //     }
    //
    //     for (int i=0; i<150;i=i+20) {
    //         set_level_template.firstbyte = (49 << 1) + 1;
    //         set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //         set_level_template.secondbyte = i;
    //         ESP_LOGI(TAG, "Set address 49 to level %d", i);
    //         ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(300));
    //
    //         set_level_template.firstbyte = (39 << 1);
    //         set_level_template.secondbyte = 3;
    //         ESP_LOGI(TAG, "Set address 39 to level %d", 3);
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(400));
    //
    //         ESP_LOGI(TAG, "Query level");
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &querylevel, sizeof(test), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(400));
    //     }
    // }
    // while (1) {
    //     for (int i=0; i<7;i=i+1){
    //
    //         set_level_template.secondbyte = 1<<i; // -7;
    //         // conf.outgoing = set_level_template;
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(set_level_template), &transmit_config));
    //
    //         ESP_LOGI(TAG, "Transmitting state %d", 1<<i);
    //         // ESP_LOGI(TAG, "done rx %u tx %u", rxdone, txdone);
    //
    //         // level = gpio_get_level(RX_GPIO);
    //         // gptimer_get_raw_count(strobetimer, &strobecount);
    //         // ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);
    //         vTaskDelay(pdMS_TO_TICKS(1000));
    //         // ESP_ERROR_CHECK(rmt_disable(rx_channel));
    //
    //         // rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
    //         // conf.outgoing = test;
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &querylevel, sizeof(test), &transmit_config));
    //         // delay 1 second
    //         ESP_LOGI(TAG, "Transmitting querylevel");
    //
    //         // ESP_LOGI(TAG, "done rx %u tx %u", rxdone, txdone);
    //         // ESP_LOGI(TAG, "Messages waiting %u", uxQueueMessagesWaiting(receive_queue));
    //
    //         vTaskDelay(pdMS_TO_TICKS(1000));
    //         // gpio_dump_io_configuration(stdout,
    //             // (1ULL << RX_GPIO) | (1ULL << 17) | (1ULL << TX_GPIO));
    //
    //         // ESP_LOGI(TAG, "done rx %u tx %u times %lu inputtimes %lu value %lu", rxdone, txdone, times, inputtimes, value);
    //         // ESP_ERROR_CHECK(rmt_enable(rx_channel));
    //         // ESP_LOG_BUFFER_HEX(TAG, raw_symbols, 32);
    //         vTaskDelay(pdMS_TO_TICKS(600));
    //     }
    // }