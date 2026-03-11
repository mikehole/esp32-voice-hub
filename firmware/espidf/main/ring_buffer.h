/**
 * Ring Buffer for streaming audio playback
 * 
 * Lock-free single-producer single-consumer ring buffer
 * optimized for audio streaming on ESP32-S3
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buffer;
    size_t size;
    volatile size_t write_pos;
    volatile size_t read_pos;
    volatile bool is_writing;   // Producer still adding data
} ring_buffer_t;

/**
 * Initialize ring buffer
 * @param rb Ring buffer structure
 * @param buffer Pre-allocated buffer (should be in PSRAM for audio)
 * @param size Buffer size in bytes
 */
void ring_buffer_init(ring_buffer_t *rb, uint8_t *buffer, size_t size);

/**
 * Reset ring buffer to empty state
 */
void ring_buffer_reset(ring_buffer_t *rb);

/**
 * Write data to ring buffer
 * @return Number of bytes written (may be less than requested if buffer full)
 */
size_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t len);

/**
 * Read data from ring buffer
 * @return Number of bytes read (may be less than requested if buffer empty)
 */
size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *data, size_t len);

/**
 * Get number of bytes available for reading
 */
size_t ring_buffer_available(const ring_buffer_t *rb);

/**
 * Get free space available for writing
 */
size_t ring_buffer_free(const ring_buffer_t *rb);

/**
 * Check if buffer is empty
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb);

/**
 * Check if producer is still writing
 */
bool ring_buffer_is_writing(const ring_buffer_t *rb);

/**
 * Mark stream as complete (producer done)
 */
void ring_buffer_end_write(ring_buffer_t *rb);

/**
 * Mark stream as started (producer beginning)
 */
void ring_buffer_start_write(ring_buffer_t *rb);

#endif // RING_BUFFER_H
