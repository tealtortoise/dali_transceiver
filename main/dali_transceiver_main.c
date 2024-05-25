#include <soc/gpio_reg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "dali_encoder.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "edgeframe_logger.c"

#define RESOLUTION_HZ     10000000
#define PWM_RESOLUTION 10

#define TX_GPIO       18
#define RX_GPIO       6

#define RECEIVE_DOUBLE_BIT_THRESHOLD 6000

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

void configure_gpio(){
    gpio_config_t gpio_17config = {
        .pin_bit_mask = 1ULL << 17,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_17config));

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

bool IRAM_ATTR timer_group0_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
    
    if (state) {
        // gpio_set_level(17, 0);
        state = 0;
    } else {
        // gpio_set_level(17, 1);
        state = 1;
    }
    
    gpio_set_level(17, state);
    return false;
}

gptimer_handle_t configure_timer(){
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 10000000, // 2s
        // .alarm_count = 0x7ff0, // 2s
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,

    };
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_group0_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    // ESP_ERROR_CHECK(gptimer_start(gptimer));
    return gptimer;
}

void configure_ledc(){
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = PWM_RESOLUTION, // resolution of PWM duty
        .freq_hz = 10000,                      // frequency of PWM signal
        .speed_mode = LEDC_LOW_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0,             // timer index
        // .clk_cfg = LEDC_USE_RC_FAST_CLK,             // Auto select the source clock
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = 1 << (PWM_RESOLUTION - 2),
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num   = 15,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        // .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// void set_gpio_task(void *pvParameter){
//     while (1){
//         gpio_set_level(17, state);
//         ESP_LOGI(TAG, "gpio17 %d", state);
//         vTaskDelay(pdMS_TO_TICKS(500));
//     }
// }

static DRAM_ATTR rmt_channel_handle_t rx_channel;

static DRAM_ATTR rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 2000,     // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
    .signal_range_max_ns = 1600000, // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
};

static DRAM_ATTR rmt_symbol_word_t raw_symbols[128];
static DRAM_ATTR rmt_rx_done_event_data_t rx_data;


void rmt_queue_log_task(QueueHandle_t *queue){

    rmt_rx_done_event_data_t receiveData;
    receive_event_t receivestruct;
    uint64_t manchester_word;
    uint8_t manchester_bits[64];
    uint32_t final_word;
    uint8_t bit_position;
    uint8_t firstbyte;
    uint8_t secondbyte;
    bool a;
    bool b;
    while (1) {

        bool received = xQueueReceive(*queue, &receivestruct, 101);

        if (received) {
            ESP_LOGI(TAG, "QUeue receive %lu", receivestruct.num);
            // receiveData = *receivestruct.event_data;
            for (int round = 1;round <2; round++) {
                // rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
                manchester_word = 0;
                for (int i=0; i< 64; i++) {
                    manchester_bits[i] = 0;
                }
                bit_position = 0;
                if (round > 0) {
                    ESP_LOGI(TAG, "Received RMT message...");
                }
                rmt_symbol_word_t *symbols = receivestruct.received_symbols;
                for (int i = 0; i < receivestruct.num_symbols; i++) {
                    rmt_symbol_word_t symbol = symbols[i];
                    if (round > 0) {
                        ESP_LOGI(TAG, "   Received RMT frame %u: %u, %u; %u", symbol.level0,
                            symbol.duration0,
                            symbol.level1,
                            symbol.duration1);
                    }
                    manchester_word = manchester_word | ((uint64_t)symbol.level0 << bit_position);
                    manchester_bits[bit_position] = symbol.level0;
                    bit_position += 1;
                    if (symbol.duration0 > RECEIVE_DOUBLE_BIT_THRESHOLD) {
                        manchester_word = manchester_word | ((uint64_t)symbol.level0 << bit_position);
                        manchester_bits[bit_position] = symbol.level0;
                        bit_position += 1;
                    }
                    manchester_word = manchester_word | ((uint64_t)symbol.level1 << bit_position);
                    manchester_bits[bit_position] = symbol.level1;
                    bit_position += 1;
                    if (symbol.duration1 > RECEIVE_DOUBLE_BIT_THRESHOLD) {
                        manchester_word = manchester_word | ((uint64_t)symbol.level1 << bit_position);
                        manchester_bits[bit_position] = symbol.level1;
                        bit_position += 1;
                    }

                }
                // ESP_LOGI(TAG, "End RMT Frames %llu", manchester_word);
                // ESP_LOG_BUFFER_HEX(TAG, &manchester_word, 8);
                // for (int i=0; i<64; i++) {
                // ESP_LOGI(TAG, "received bit %u: %u", i, manchester_bits[i]);
                // }
                final_word = 0;
                a = 0;
                b = 0;
                for (uint8_t i = 0; i <16; i += 1) {
                    a = manchester_bits[i * 2 + 1];
                    b = manchester_bits[i * 2 + 2];
                    // ESP_LOGI(TAG, "i, a, b: %u, %u, %u", i, a, b);
                    if (a != b) {
                        final_word = final_word | (a << (15-i));
                    }
                    else {
                        ESP_LOGE(TAG, "Bit error");
                    }
                }
                secondbyte = final_word & 0xFF;
                firstbyte = (final_word & 0xFF00) >> 8;
                if (round > 0) {
                    ESP_LOGI(TAG, "Received first byte %u", firstbyte);
                    ESP_LOGI(TAG, "Received second byte %u", secondbyte);
                }
                if (firstbyte == receivestruct.outgoing.firstbyte && secondbyte == receivestruct.outgoing.secondbyte) {
                    ESP_LOGI(TAG, "First byte matches");
                    ESP_LOGI(TAG, "Second byte matches");
                    break;
                }
            }
        }

    }
}

void add_dummy_queue_items(QueueHandle_t *queue) {
    rmt_symbol_word_t symbol = {
        .level0 = 0,
        .duration0 = 123,
        .level1 = 1,
        .duration1 = 456
    };
    rmt_symbol_word_t symbol2 = {
        .level0 = 1,
        .duration0 = 321,
        .level1 = 0,
        .duration1 = 9870
    };
    rmt_symbol_word_t dummy_symbols[] = {symbol, symbol, symbol2};
    rmt_rx_done_event_data_t sendData = {
        .received_symbols = dummy_symbols,
        .num_symbols = 3,
    };

    while (1) {
        vTaskDelay(100);
        vTaskDelay(pdMS_TO_TICKS(1000));
        BaseType_t result = xQueueSend(*queue,(void *) &sendData, portMAX_DELAY);
        if (result != pdTRUE) {
            ESP_LOGI(TAG, "Queue send Failed");
        }
    }

}

static int DRAM_ATTR rxdone = 0;
static int DRAM_ATTR txdone = 0;



typedef struct {
    rmt_channel_handle_t rxc;
    rmt_symbol_word_t* rs[128];
    rmt_receive_config_t* rc;
    QueueHandle_t q;
    dali_forward_frame_t outgoing;
} passs;

static bool IRAM_ATTR example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    passs conf = *(passs*) user_data;
    // QueueHandle_t receive_queue = (QueueHandle_t)user_data;

    // send the received RMT symbols to the parser task
    rxdone += 1;

    receive_event_t send_data = {
        .received_symbols = edata->received_symbols,
        .num_symbols = edata->num_symbols,
        .outgoing = conf.outgoing,
        .num = rxdone
    };

    xQueueSendFromISR(conf.q, &send_data, &high_task_wakeup);

    static rmt_receive_config_t receive_config_ = {
        .signal_range_min_ns = 2000,     // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
        .signal_range_max_ns = 1600000, // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
    };
    rmt_receive(conf.rxc, conf.rs, sizeof(conf.rs), conf.rc);
    // rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config_);
    return high_task_wakeup == pdTRUE;
}


static bool IRAM_ATTR example_rmt_tx_done_callback(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *edata, void *user_data)
{
    // BaseType_t high_task_wakeup = pdFALSE;
    // rmt_channel_handle_t rx_channel = user_data;
    // rmt_enable(rx_channel);
    txdone += 1;
    return false;
}


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
        if (times < 204) {
            ESP_DRAM_LOGI(TAG, "Received Timer ISR first %d second %d , time (us) %llu", firstbyte, secondbyte, count - startcount);
        }
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

    // ESP_LOGI(TAG, "Tart Start");
    // ESP_ERROR_CHECK(gptimer_start(gptimer));
    return gptimer;
}

#define EDGEDECODE_STATE_INIT 0
#define EDGEDECODE_STATE_CHECKSTART 1
#define EDGEDECODE_STATE_BITREADY 2
#define EDGEDECODE_STATE_END 3

#define DALI_TIMING_IDEA_HALF_BIT_US 417
#define DALI_TIMING_TOLERANCE_US 150


bool check_if_half_period(uint16_t time) {
    return time < DALI_TIMING_IDEA_HALF_BIT_US + DALI_TIMING_TOLERANCE_US && time > DALI_TIMING_IDEA_HALF_BIT_US - DALI_TIMING_TOLERANCE_US;
}

bool check_if_full_period(uint16_t time) {
    return time < DALI_TIMING_IDEA_HALF_BIT_US * 2 + DALI_TIMING_TOLERANCE_US && time > DALI_TIMING_IDEA_HALF_BIT_US * 2 - DALI_TIMING_TOLERANCE_US;
}

void edgeframe_queue_log_task(QueueHandle_t *queue) {
    edgeframe receivedframe;
    while (1) {
        bool received = xQueueReceive(*queue, &receivedframe, 101);
        if (received) {
            ESP_LOGI(TAG, "Received frame length %d", receivedframe.length);
            uint8_t state = 0;
            uint32_t output = 0;
            uint8_t output_bit_pos = 23;
            edge_t edge;
            uint16_t last_valid_bit_time;
            uint16_t last_valid_bit_elapsed;
            uint16_t last_edge_elapsed = 0;
            bool error = false;
            for (uint8_t i = 0; i<receivedframe.length; i++) {
                ESP_LOGI(TAG, "Level %d at %u us (%u us) state %d",
                    receivedframe.edges[i].edgetype,
                    receivedframe.edges[i].time,
                    receivedframe.edges[i].time - last_edge_elapsed,
                    state);
                last_edge_elapsed = receivedframe.edges[i].time;
                if (error || state == EDGEDECODE_STATE_END) break;
                edge = receivedframe.edges[i];
                switch (state) {
                    case EDGEDECODE_STATE_INIT: {
                        state = EDGEDECODE_STATE_CHECKSTART;
                        break;
                    }
                    case EDGEDECODE_STATE_CHECKSTART: {
                        if (edge.edgetype == EDGETYPE_RISING && check_if_half_period(edge.time)) {
                            state = EDGEDECODE_STATE_BITREADY;
                            last_valid_bit_time = edge.time;
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Start bit error %u", i);
                            error = true;
                        }
                        break;
                    }
                    case EDGEDECODE_STATE_BITREADY: {

                        if (edge.edgetype == EDGETYPE_NONE) {
                            state = EDGEDECODE_STATE_END;
                            break;
                        }
                        last_valid_bit_elapsed = edge.time - last_valid_bit_time;
                        if (check_if_half_period(last_valid_bit_elapsed)) {
                            ESP_LOGI(TAG, "...Half bit %u", i);
                            // do nothing
                        }
                        else if (check_if_full_period(last_valid_bit_elapsed))
                        {
                            ESP_LOGI(TAG, "...Full bit %u edge %d bitpos %d", i, edge.edgetype, output_bit_pos);
                            if (edge.edgetype == EDGETYPE_RISING) {
                                output = output | (1 << output_bit_pos);
                            }
                            last_valid_bit_time = edge.time;
                            output_bit_pos -= 1;
                        }
                        else
                        {
                            ESP_LOGE(TAG, "No bit error %u elapsed %u", i, last_valid_bit_elapsed);
                            error = true;
                        }
                        break;
                    }
                }
            }
            ESP_LOGI(TAG, "Final output %lu", output);
            ESP_LOGI(TAG, "Final output  >> 8 %lu", output >> 8);
            uint8_t firstbyte = (output & 0xFF0000) >> 16;
            uint8_t secondbyte = (output & 0xFF00) >> 8;
            ESP_LOGI(TAG, "First %d, second %d", firstbyte, secondbyte);
            char bitstring[30];
            // bitstring[24] = 0;
            int spaceadd = 0;
            for (int i = 0; i<16;i++) {
                if (i == 8) {
                    spaceadd += 1;
                    bitstring[i] = 32;
                }
                bitstring[i + spaceadd] = ((output >> (23 - i)) & 1) ? 49 : 48;
            }
            bitstring[16 + spaceadd] = 0;
            ESP_LOGI(TAG, "Bitstring %s", bitstring);
                // ESP_LOGI(TAG, "Level %d after %u us",
                    // receivedframe.edges[i].edgetype,
                    // receivedframe.edges[i].time);
            // }
        }
    }
}

void app_main(void)
{
    //
    // ESP_LOGI(TAG, "create RMT RX channel");
    // rmt_rx_channel_config_t rx_channel_cfg = {
    //     .clk_src = RMT_CLK_SRC_DEFAULT,
    //     .resolution_hz = RESOLUTION_HZ,
    //     .mem_block_symbols = 128, // amount of RMT symbols that the channel can store at a time
    //     .gpio_num = RX_GPIO,
    //     .flags.io_loop_back = 0,
    //     .flags.invert_in = 0,
    // };
    // rmt_channel_handle_t rx_channel = NULL;
    // ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));
    //
    QueueHandle_t receive_queue = xQueueCreate(3, sizeof(receive_event_t));
    assert(receive_queue);

    // rmt_rx_event_callbacks_t cbs = {
    //     .on_recv_done = example_rmt_rx_done_callback,
    // };
    //
    // passs conf = {{rx_channel},{raw_symbols},{&receive_config},{receive_queue}};
    // ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, &conf));


    // ESP_ERROR_CHECK(rmt_enable(rx_channel));


    // ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));

    configure_gpio();

    gptimer_handle_t strobetimer = configure_timer();
    ESP_ERROR_CHECK(gptimer_start(strobetimer));
    configure_ledc();

    int level;
    uint64_t strobecount;

    level = gpio_get_level(RX_GPIO);
    gptimer_get_raw_count(strobetimer, &strobecount);
    ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);

    gptimer_handle_t tart_timer = configure_tart_timer(strobetimer);
    // ESP_ERROR_CHECK(gptimer_start(tart_timer));
    vTaskDelay(2);
    tarttimerh = tart_timer;
    uint64_t count;
    ESP_ERROR_CHECK(gptimer_get_raw_count(tart_timer, &count));
    ESP_LOGI(TAG, "raw count %llu", count);
    // gptimer_enable(tart_timer);
    // ESP_LOGI(TAG, "Tart timer %lu", tart_timer);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    QueueHandle_t edgeframe_queue = start_edgelogger(RX_GPIO, true);
    // gpio_set_intr_type(RX_GPIO, GPIO_INTR_POSEDGE);
    // gpio_set_i
    // ESP_ERROR_CHECK(gpio_isr_handler_add(RX_GPIO, input_isr, tart_timer));



    level = gpio_get_level(RX_GPIO);
    gptimer_get_raw_count(strobetimer, &strobecount);
    ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);


    ESP_LOGI(TAG, "create RMT TX channel");
    rmt_tx_channel_config_t tx_channel_cfg = {
        // .clk_src = RMT_CLK_SRC_RC_FAST,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .flags = {
            .invert_out = 1,
            .with_dma = 0,
            .io_loop_back = 0,
            .io_od_mode = 0,   // open drain mode is disabled
        },
        .resolution_hz = RESOLUTION_HZ,
        .mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
        .trans_queue_depth = 4,  // number of transactions that allowed to pending in the background, this example won't queue multiple transactions, so queue depth > 1 is sufficient
        .gpio_num = TX_GPIO,
    };
    rmt_channel_handle_t tx_channel = NULL;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &tx_channel));

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // no loop
        .flags = {
            .eot_level = 1, // send EOT signal at the end of sending a frame
            .queue_nonblocking = 0, // block waiting for the transmission to be done
        },
    };

    ESP_LOGI(TAG, "install IR dali encoder");
    dali_encoder_config_t dali_encoder_cfg = {
        .resolution = RESOLUTION_HZ,
    };
    rmt_encoder_handle_t dali_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_dali_encoder(&dali_encoder_cfg, &dali_encoder));

    rmt_tx_event_callbacks_t txcbs = {
        .on_trans_done = example_rmt_tx_done_callback,
    };

    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(tx_channel, &txcbs, rx_channel));

    ESP_LOGI(TAG, "enable RMT TX channel");

    ESP_ERROR_CHECK(rmt_enable(tx_channel));
    // ESP_ERROR_CHECK(rmt_enable(rx_channel));

    level = gpio_get_level(RX_GPIO);
    gptimer_get_raw_count(strobetimer, &strobecount);
    ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);


    dali_forward_frame_t set_level_template = {
        .firstbyte = 0b11111110,
        .secondbyte = NULL,
    };
    dali_forward_frame_t reset = {
        .firstbyte = 0b11111111,
        .secondbyte = 0x20,
    };
    dali_forward_frame_t off = {
        .firstbyte = 0b11111111,
        .secondbyte = 0,
    };

    dali_forward_frame_t set_dtr0 = {
        .firstbyte = 0b10100011,
        .secondbyte = NULL,
    };
    dali_forward_frame_t query = {
        .firstbyte = DALI_FIRSTBYTE_BROADCAST_COMMAND,
        .secondbyte = 0x90,
    };
    dali_forward_frame_t querylevel = {
        .firstbyte = DALI_FIRSTBYTE_BROADCAST_COMMAND,
        .secondbyte = 0b10100000,
    };

    dali_forward_frame_t test = {
        .firstbyte = 0b01001100, // 76
        .secondbyte = 0b10011100, //156
    };
    dali_forward_frame_t testff = {
        .firstbyte = 0xFF, // 76
        .secondbyte = 0xFF, //156
    };

    // xTaskCreate((TaskFunction_t *) rmt_queue_log_task, "rmt_queue_log_task", 9128, &receive_queue, 1, NULL);
    xTaskCreate(edgeframe_queue_log_task, "edgeframe_queue_log_task", 9128, &edgeframe_queue, 1, NULL);
    // xTaskCreate((TaskFunction_t *) add_dummy_queue_items, "add_dummy_queue_items", 2048, &receive_queue, 2, NULL);


    // uint64_t count;
    // ESP_ERROR_CHECK(gptimer_get_raw_count(tart_timer, &count));
    // ESP_LOGI(TAG, "tart count %llu", count);
    // passs conf = {
    //     .rc = &receive_config,
    //     .rs = raw_symbols,
    //     .rxc = rx_channel,
    // };
    // rmt_receive(conf.rxc, conf.rs, sizeof(conf.rs), conf.rc);

    level = gpio_get_level(RX_GPIO);
    gptimer_get_raw_count(strobetimer, &strobecount);
    ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);

    while (1) {
        for (int i=0; i<7;i=i+1){

            set_level_template.secondbyte = 1<<i; // -7;
            // conf.outgoing = set_level_template;
            ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(set_level_template), &transmit_config));

            ESP_LOGI(TAG, "Transmitting state %d", 1<<i);
            // ESP_LOGI(TAG, "done rx %u tx %u", rxdone, txdone);

            // level = gpio_get_level(RX_GPIO);
            // gptimer_get_raw_count(strobetimer, &strobecount);
            // ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, i << (PWM_RESOLUTION - 8)));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
            // ESP_ERROR_CHECK(rmt_disable(rx_channel));

            // rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
            // conf.outgoing = test;
            ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &querylevel, sizeof(test), &transmit_config));
            // delay 1 second
            ESP_LOGI(TAG, "Transmitting querylevel");

            // ESP_LOGI(TAG, "done rx %u tx %u", rxdone, txdone);
            // ESP_LOGI(TAG, "Messages waiting %u", uxQueueMessagesWaiting(receive_queue));

            vTaskDelay(pdMS_TO_TICKS(1000));
            // gpio_dump_io_configuration(stdout,
                // (1ULL << RX_GPIO) | (1ULL << 17) | (1ULL << TX_GPIO));

            // ESP_LOGI(TAG, "done rx %u tx %u times %lu inputtimes %lu value %lu", rxdone, txdone, times, inputtimes, value);
            // ESP_ERROR_CHECK(rmt_enable(rx_channel));
            // ESP_LOG_BUFFER_HEX(TAG, raw_symbols, 32);
            vTaskDelay(pdMS_TO_TICKS(600));
        }
    }
}
