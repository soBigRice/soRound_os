#pragma once
#include <stdbool.h>
#include <stdint.h>

// One boot request, captured at final-tile submission and acknowledged by DMA.
// Rendering is serialized by LVGL; completion runs in the panel ISR.
typedef struct {
    volatile uint32_t requested,submitted,completed;
} display_frame_gate_t;
static inline uint32_t display_frame_gate_request(display_frame_gate_t *gate) {
    uint32_t request=gate->requested+1;if(!request)request=1;
    gate->requested=request;return request;
}
static inline void display_frame_gate_submit(display_frame_gate_t *gate,bool last) {
    gate->submitted=last?gate->requested:0;
}
static inline void display_frame_gate_complete(display_frame_gate_t *gate) {
    if(gate->submitted){gate->completed=gate->submitted;gate->submitted=0;}
}
static inline bool display_frame_gate_done(const display_frame_gate_t *gate,uint32_t request) {
    return request && gate->completed==request;
}
