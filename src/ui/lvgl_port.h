#pragma once

#include <stdint.h>
#include <stdbool.h>

// LilyGO-style LVGL 9 port over existing esp_lcd ST7789 panel.
bool lvgl_port_init(void);
void lvgl_port_handler(void);
bool lvgl_port_ready(void);

/** Free the INTERNAL+DMA draw buffer while SoftAP is up (no cabin paint). */
bool lvgl_port_suspend_draw_buf(void);
/** Re-alloc draw buffer before cabin UI resumes. */
bool lvgl_port_resume_draw_buf(void);

// Counters: DMA completion ISR vs flush_cb entries (stability gate).
uint32_t lvgl_port_dma_isr_count(void);
uint32_t lvgl_port_flush_enter_count(void);
uint32_t lvgl_port_dma_timeout_count(void);
