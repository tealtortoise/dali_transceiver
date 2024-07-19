#ifndef REALTIME_H
#define REALTIME_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void setup_sntp(TaskHandle_t mainlooptask);

#endif // REALTIME_H
#pragma once
