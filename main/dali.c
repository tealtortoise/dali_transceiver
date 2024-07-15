#include <stdint.h>
#include "dali.h"
#include "esp_log.h"


static const char *TAG = "DALI";

uint8_t get_dali_address_byte(uint8_t address){
    return (address << 1) + 1;
}
uint8_t get_dali_address_byte_setlevel(uint8_t address){
    return (address << 1);
}

const char *frametypes[4] = {"Forward frame", "Backward frame", "Mangled frame", "No frame"};

void dali_log_frame(dali_frame_t frame){
    log_dali_frame_prefix(frame, "");
}


void log_dali_frame_prefix(dali_frame_t frame, char* prefix){
    uint16_t output = frame.secondbyte | (frame.firstbyte << 8);
        char bitstring[30];
    // bitstring[24] = 0;
    int spaceadd = 0;
    for (int i = 0; i<16;i++) {
        if (i == 8) {
            spaceadd += 1;
            bitstring[i] = 32;
        }
        bitstring[i + spaceadd] = ((output >> (15 - i)) & 1) ? 49 : 48;
    }
    bitstring[16 + spaceadd] = 0;
    switch (frame.type)
    {
    case DALI_FORWARD_FRAME_TYPE:
        ESP_LOGI(TAG, "%sDALI Forward Frame: %d, %d |%s|", prefix, frame.firstbyte, frame.secondbyte, bitstring);
        break;
    
    case DALI_BACKWARD_FRAME_TYPE:
        ESP_LOGI(TAG, "%sDALI Backward Frame: %d |%s|",prefix,  frame.firstbyte, bitstring);
        break;
    case DALI_MANGLED_FRAME:
        ESP_LOGI(TAG, "%sDALI Mangled Frame: %d, %d |%s|", prefix, frame.firstbyte, frame.secondbyte, bitstring);
        break;
    case DALI_NO_FRAME_TYPE:
        ESP_LOGI(TAG, "%sDALI Non-existent Frame: %d, %d |%s|", prefix, frame.firstbyte, frame.secondbyte, bitstring);
        break;
    default:
        break;
    }
}