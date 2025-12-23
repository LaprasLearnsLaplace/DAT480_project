#pragma once

#include "hls_task.h"
#include "ap_int.h"
#include <stdint.h>

#define MAX_SAMPLES 1023

#define TEST_SIZE 10000

struct sample {
    uint32_t cycles;
    uint32_t bytes;
    uint32_t ready_not_valid;
    uint32_t valid_not_ready;
};

struct config {
    uint32_t num_samples;
    uint32_t rate;
};
