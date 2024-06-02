#include <soc/gpio_reg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "edgeframe_logger.c"
#include "dali_transmit.c"
#include "0-10v.c"
#include "dali_edgeframe_parser.c"
#include "dali_rmt_receiver.c"
#include "wifi.c"
#include "http_server.c"

#define RESOLUTION_HZ     10000000

#define TX_GPIO       18
#define STROBE_GPIO 17
#define RX_GPIO       18
#define PWM_010v_GPIO   15


#define DALI_FIRSTBYTE_BROADCAST_LEVEL 0b11111110
#define DALI_FIRSTBYTE_BROADCAST_COMMAND 0b11111111

#define DALI_SECONDBYTE_COMMAND_RESET 0x20
#define DALI_SECONDBYTE_COMMAND_OFF 0x00


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



static const char *TAG = "example";

// esp_log_level_set(TAG, ESP_LOG_DEBUG);

void configure_gpio(){
    gpio_config_t strobe_gpio_config = {
        .pin_bit_mask = 1ULL << 17,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&strobe_gpio_config));
    ESP_ERROR_CHECK(gpio_set_drive_capability(STROBE_GPIO, GPIO_DRIVE_CAP_3));

    gpio_config_t gpio_txconfig = {
        .pin_bit_mask = 1ULL << TX_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_txconfig));
    ESP_ERROR_CHECK(gpio_set_drive_capability(TX_GPIO, GPIO_DRIVE_CAP_3));
    gpio_set_level(TX_GPIO, 0);

    gpio_config_t gpio_6config = {
        .pin_bit_mask = 1ULL << RX_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_6config));
    ESP_LOGI(TAG, "GPIO %u: level %u", RX_GPIO, gpio_get_level(RX_GPIO));
}

static volatile uint8_t state = 0;

bool IRAM_ATTR strobetimer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
    if (state) {
        state = 0;
    } else {
        state = 1;
    }
    gpio_set_level(STROBE_GPIO, state);
    return false;
}

gptimer_handle_t configure_strobetimer(){
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 8000000, // 2s
        // .alarm_count = 0x7ff0, // 2s
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,

    };
    gptimer_event_callbacks_t cbs = {
        .on_alarm = strobetimer_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    // ESP_ERROR_CHECK(gptimer_start(gptimer));
    return gptimer;
}


static DRAM_ATTR rmt_channel_handle_t rx_channel;

static DRAM_ATTR rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 2000,     // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
    .signal_range_max_ns = 1600000, // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
};

static DRAM_ATTR rmt_symbol_word_t raw_symbols[128];
static DRAM_ATTR rmt_rx_done_event_data_t rx_data;

//
//
//
static int DRAM_ATTR rxdone = 0;
static int DRAM_ATTR txdone = 0;

static volatile DRAM_ATTR uint32_t times =0;
static volatile DRAM_ATTR uint32_t inputtimes =0;
static volatile DRAM_ATTR uint32_t tarttimer =0;
static volatile DRAM_ATTR gptimer_handle_t tarttimerh;

void IRAM_ATTR input_isr(void *params) {
    gptimer_handle_t timer = (gptimer_handle_t) params;
    rxdone += 1;
    // gptimer_handle_t timer = tarttimerh;
    tarttimer = (uint32_t)timer;
    // ESP_DRAM_LOGI(TAG, "Tart timer in GPIO ISR %lu", (uint32_t)timer);
    // ESP_LOGI(TAG, "ISR tart alarm count %llu", timer->alarm_count);
    // gptimer_enable(timer);
    // ESP_DRAM_LOGI(TAG, "Tart Set count");
    // ESP_ERROR_CHECK(gptimer_enable(timer));
    // ESP_ERROR_CHECK(gptimer_set_raw_count(timer, 1000000/4800));
    gptimer_set_raw_count(timer, 208);
    gptimer_start(timer);
    // uint64_t count;
    // ESP_ERROR_CHECK(gptimer_get_raw_count(timer, &count));
    // ESP_DRAM_LOGI(TAG, "GPIO ISR raw count %llu", count);

    // uint64_t count;
    // ESP_ERROR_CHECK(gptimer_get_raw_count(timer, &count));
    // ESP_DRAM_LOGI(TAG, "tart count %llu", count);
    inputtimes += 1;
    gpio_set_intr_type(RX_GPIO, GPIO_INTR_DISABLE);

}

static volatile DRAM_ATTR uint32_t value = 0;
static volatile DRAM_ATTR uint64_t startcount = 0;
static volatile DRAM_ATTR int last = 0;
static volatile DRAM_ATTR int8_t error = 0;
static volatile DRAM_ATTR int8_t run = 0;

bool IRAM_ATTR input_timer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    if (times == 0) {
        value = 0;
        uint64_t count;
        gptimer_get_raw_count((gptimer_handle_t) user_ctx, &count);
        startcount = count;
        error = 0;
        run = 0;
    }
    if (times > 1 && times < 44 && run < 2) { //(times & 1)
        if ((times & 1) == 1) {
            int level = gpio_get_level(RX_GPIO);
            // uint64_t count;
            // gptimer_get_raw_count((gptimer_handle_t) user_ctx, &count);
            // ESP_DRAM_LOGI(TAG, "TIMER ISR %lu, %u: count %llu", times, level, count);

            if (level == last) {
                if (level == 0 && times > 18) {
                    run += 1;
                }
                else {
                    error += 1;
                }
            } else {
                if (level == 0) {
                    value = value | (1 << (24 - (times / 2)));
                }
            }
        }
        else {
            int level = gpio_get_level(RX_GPIO);
            last = level;
        }
    }
    if (times == 44 || error >= 1 || run >= 2) {
        gptimer_stop(timer);
        uint64_t count;
        gptimer_get_raw_count((gptimer_handle_t) user_ctx, &count);
        uint8_t firstbyte;
        uint8_t secondbyte;
        secondbyte = (value & 0xff00) >> 8;
        firstbyte = (value & 0xff0000) >> 16;
        // if (times < 204) {
            // ESP_DRAM_LOGI(TAG, "Received Timer ISR first %d second %d , time (us) %llu", firstbyte, secondbyte, count - startcount);
        // }
        gpio_set_intr_type(RX_GPIO, GPIO_INTR_POSEDGE);
        if (error != 0) {
            ESP_DRAM_LOGI(TAG, "Error %d", error);
        }
        if (run < 2) {
            ESP_DRAM_LOGI(TAG, "No stop condition Error %d", run);
        }

        times = 0;
    }
    else {
        times += 1;

    }
    return false;
}

gptimer_handle_t configure_tart_timer(gptimer_handle_t strobetimer){
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 412,
        // .alarm_count = 0x7ff0,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,

    };
    gptimer_event_callbacks_t cbs = {
        .on_alarm = input_timer_isr,
    };
    ESP_LOGI(TAG, "Tart Register callbacks");
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, strobetimer));

    ESP_LOGI(TAG, "Tart Enable");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Tart Set alarm actions");
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    return gptimer;
}
//
// rmt_transmit_config_t configure_rmt_tx(rmt_channel_handle_t *tx_channel, rmt_encoder_handle_t *dali_encoder) {
//
//     ESP_LOGI(TAG, "create RMT TX channel");
//     rmt_tx_channel_config_t tx_channel_cfg = {
//         // .clk_src = RMT_CLK_SRC_RC_FAST,
//         .clk_src = RMT_CLK_SRC_DEFAULT,
//         .flags = {
//             .invert_out = 0,
//             .with_dma = 0,
//             .io_loop_back = 0,
//             .io_od_mode = 0,   // open drain mode is disabled
//         },
//         .resolution_hz = RESOLUTION_HZ,
//         .mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
//         .trans_queue_depth = 4,  // number of transactions that allowed to pending in the background, this example won't queue multiple transactions, so queue depth > 1 is sufficient
//         .gpio_num = TX_GPIO,
//     };
//     // rmt_channel_handle_t tx_channel = NULL;
//     ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, tx_channel));
//
//     rmt_transmit_config_t transmit_config = {
//         .loop_count = 0, // no loop
//         .flags = {
//             .eot_level = 0, // send EOT signal at the end of sending a frame
//             .queue_nonblocking = 0, // block waiting for the transmission to be done
//         },
//     };
//
//     ESP_LOGI(TAG, "install IR dali encoder");
//     dali_encoder_config_t dali_encoder_cfg = {
//         .resolution = RESOLUTION_HZ,
//     };
//     // rmt_encoder_handle_t dali_encoder = NULL;
//     ESP_ERROR_CHECK(rmt_new_dali_encoder(&dali_encoder_cfg, dali_encoder));
//
//     rmt_tx_event_callbacks_t txcbs = {
//         .on_trans_done = example_rmt_tx_done_callback,
//     };
//
//     ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(*tx_channel, &txcbs, rx_channel));
//
//     ESP_LOGI(TAG, "enable RMT TX channel");
//
//     ESP_ERROR_CHECK(rmt_enable(*tx_channel));
//     dali_forward_frame_t frame = {.firstbyte = 0, .secondbyte = 0};
//     // ESP_ERROR_CHECK(rmt_transmit(*tx_channel, *dali_encoder, &frame, sizeof(frame), &transmit_config));
//     return transmit_config;
// }

void setup_events(){
    esp_event_loop_args_t eventloopconfig = {
        .queue_size = 5,
    };
    esp_event_handler_t lightingloop;
    esp_event_loop_create(&eventloopconfig, &lightingloop);

}


void app_main(void)
{
    configure_gpio();
    setup_wifi();
    httpd_handle_t httpd = setup_httpserver();
    
    gptimer_handle_t strobetimer = configure_strobetimer();
    ESP_ERROR_CHECK(gptimer_start(strobetimer));
    ledc_channel_t pwm0_10v_channel1;
    setup_pwm_0_10v();
    dali_transmitter_handle_t dali_transmitter_handle;
    setup_dali_transmitter(TX_GPIO, 3, &dali_transmitter_handle);
    
    ESP_ERROR_CHECK(configure_pwm_0_10v(PWM_010v_GPIO, 1<<14, &pwm0_10v_channel1));

    int level;
    uint64_t strobecount;
    //
    // rmt_channel_handle_t tx_channel;
    // rmt_encoder_handle_t dali_encoder;
    // rmt_transmit_config_t transmit_config = configure_rmt_tx(&tx_channel, &dali_encoder);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    // QueueHandle_t rmt_frame_queue = setup_rmt_dali_receiver(RX_GPIO, 4);
    // setup_rmt_log_task(rmt_frame_queue);
    
    QueueHandle_t edgeframe_queue = start_edgelogger(RX_GPIO, false);
    // ESP_LOGI(TAG, "Queuehandle %u", edgeframe_);
    start_dali_parser(edgeframe_queue);

    level = gpio_get_level(RX_GPIO);
    gptimer_get_raw_count(strobetimer, &strobecount);
    ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);


    while (1){
        BaseType_t success;
        dali_forward_frame_t frame;
        frame.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
        for (int i=0; i<255; i++) {
            // update_0_10v_level(pwm0_10v_channel1, i *i);
            frame.secondbyte = i;
            success = xQueueSendToBack(dali_transmitter_handle.queue, &frame, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(1000));
            // success = xQueueSendToBack(dali_transmitter_handle.queue, &frame, portMAX_DELAY);
            // vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    // dali_forward_frame_t set_level_template = {
    //     .firstbyte = 0b11111110,
    //     .secondbyte = NULL,
    // };
    // dali_forward_frame_t reset = {
    //     .firstbyte = 0b11111111,
    //     .secondbyte = 0x20,
    // };
    // dali_forward_frame_t off = {
    //     .firstbyte = 0b11111111,
    //     .secondbyte = 0,
    // };
    //
    // dali_forward_frame_t set_dtr0 = {
    //     .firstbyte = 0b10100011,
    //     .secondbyte = NULL,
    // };
    // dali_forward_frame_t query = {
    //     .firstbyte = DALI_FIRSTBYTE_BROADCAST_COMMAND,
    //     .secondbyte = 0x90,
    // };
    // dali_forward_frame_t querylevel = {
    //     .firstbyte = DALI_FIRSTBYTE_BROADCAST_COMMAND,
    //     .secondbyte = 0b10100000,
    // };
    //
    // dali_forward_frame_t test = {
    //     .firstbyte = 0b01001100, // 76
    //     .secondbyte = 0b10011100, //156
    // };
    // dali_forward_frame_t testff = {
    //     .firstbyte = 0xFF, // 76
    //     .secondbyte = 0xFF, //156
    // };
    // dali_forward_frame_t queryshortaddress = {
    //     .firstbyte = 0b10111011, // 76
    //     .secondbyte = 0x00, //156
    // };
    // dali_forward_frame_t query_dtr = {
    //     .firstbyte = DALI_FIRSTBYTE_BROADCAST_COMMAND, // 76
    //     .secondbyte = 0b10011000, //156
    // };
    // dali_forward_frame_t query_dtr_49 = {
    //     .firstbyte = (49 << 1) + 1,
    //     .secondbyte = 0b10011000,
    // };
    // dali_forward_frame_t storedtr0asshortaddress = {
    //     //YAAA AAA1 1000 0000
    //     .firstbyte = DALI_FIRSTBYTE_BROADCAST_COMMAND,
    //     .secondbyte = 0b10000000,
    // };
    //
    //
    //
    // // // gpio_set_level(TX_GPIO, 0);
    // // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    // // vTaskDelay(pdMS_TO_TICKS(300));
    // // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    // // vTaskDelay(pdMS_TO_TICKS(300));
    // // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    // // while (1) {
    // // vTaskDelay(pdMS_TO_TICKS(300));
    // // }
    // // xTaskCreate((TaskFunction_t *) rmt_queue_log_task, "rmt_queue_log_task", 9128, &receive_queue, 1, NULL);
    // // xTaskCreate((TaskFunction_t *) add_dummy_queue_items, "add_dummy_queue_items", 2048, &receive_queue, 2, NULL);
    //
    // level = gpio_get_level(RX_GPIO);
    // gptimer_get_raw_count(strobetimer, &strobecount);
    // ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);
    //
    // set_level_template.firstbyte = (49 << 1);
    // set_level_template.secondbyte = 0;
    // ESP_LOGI(TAG, "Set address 49 to level %d", 0);
    // // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    // vTaskDelay(pdMS_TO_TICKS(600));
    // set_level_template.firstbyte = (49 << 1);
    // set_level_template.secondbyte = 0;
    // ESP_LOGI(TAG, "Set address 49 to level %d", 0);
    // // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    // vTaskDelay(pdMS_TO_TICKS(600));
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
}
