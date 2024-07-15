#ifndef EFL_H
#define EFL_H

#include <stdint.h>
#include <freertos/queue.h>


#define DALI_DO_INVERT 1
#define DALI_DONT_INVERT 0

QueueHandle_t start_edgelogger(uint8_t gpio, bool invert);
#pragma once
#endif