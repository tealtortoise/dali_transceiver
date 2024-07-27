#ifndef EFL_H
#define EFL_H

#include <stdint.h>
#include <freertos/queue.h>
#include "dali.h"


#define DALI_DO_INVERT 1
#define DALI_DONT_INVERT 0

edgeframe_isr_ctx_t* setup_edgelogger(uint8_t gpio, bool invert, bool enabled);
#pragma once
#endif

