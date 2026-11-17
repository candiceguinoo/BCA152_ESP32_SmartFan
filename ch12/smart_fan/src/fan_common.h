#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum { SRC_BUTTON, SRC_CLOUD } fan_src_t;

typedef struct {
    int level;        // 0, 33, 66 or 100
    fan_src_t src;    // who asked for it
} fan_cmd_t;

extern QueueHandle_t fan_cmd_queue;   // button task and cloud task -> fan task
extern QueueHandle_t fan_pub_queue;   // fan task -> cloud task (latest level to publish)

int fan_get_level(void);

// 0 -> 0, 1..33 -> 33, 34..66 -> 66, above 66 -> 100 (same rule as the Alexa Lambda)
static inline int fan_normalize_level(int v)
{
    if (v <= 0) return 0;
    if (v <= 33) return 33;
    if (v <= 66) return 66;
    return 100;
}