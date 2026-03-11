/**
 * Ring Buffer implementation for streaming audio
 */

#include "ring_buffer.h"
#include <string.h>

void ring_buffer_init(ring_buffer_t *rb, uint8_t *buffer, size_t size) {
    rb->buffer = buffer;
    rb->size = size;
    rb->write_pos = 0;
    rb->read_pos = 0;
    rb->is_writing = false;
}

void ring_buffer_reset(ring_buffer_t *rb) {
    rb->write_pos = 0;
    rb->read_pos = 0;
    rb->is_writing = false;
}

size_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t len) {
    size_t free_space = ring_buffer_free(rb);
    size_t to_write = (len < free_space) ? len : free_space;
    
    if (to_write == 0) return 0;
    
    size_t write_pos = rb->write_pos;
    size_t first_chunk = rb->size - write_pos;
    
    if (first_chunk >= to_write) {
        // Single copy
        memcpy(rb->buffer + write_pos, data, to_write);
    } else {
        // Wrap around
        memcpy(rb->buffer + write_pos, data, first_chunk);
        memcpy(rb->buffer, data + first_chunk, to_write - first_chunk);
    }
    
    // Memory barrier before updating write position
    __sync_synchronize();
    rb->write_pos = (write_pos + to_write) % rb->size;
    
    return to_write;
}

size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *data, size_t len) {
    size_t available = ring_buffer_available(rb);
    size_t to_read = (len < available) ? len : available;
    
    if (to_read == 0) return 0;
    
    size_t read_pos = rb->read_pos;
    size_t first_chunk = rb->size - read_pos;
    
    if (first_chunk >= to_read) {
        // Single copy
        memcpy(data, rb->buffer + read_pos, to_read);
    } else {
        // Wrap around
        memcpy(data, rb->buffer + read_pos, first_chunk);
        memcpy(data + first_chunk, rb->buffer, to_read - first_chunk);
    }
    
    // Memory barrier before updating read position
    __sync_synchronize();
    rb->read_pos = (read_pos + to_read) % rb->size;
    
    return to_read;
}

size_t ring_buffer_available(const ring_buffer_t *rb) {
    size_t write_pos = rb->write_pos;
    size_t read_pos = rb->read_pos;
    
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    } else {
        return rb->size - read_pos + write_pos;
    }
}

size_t ring_buffer_free(const ring_buffer_t *rb) {
    // Leave 1 byte to distinguish full from empty
    return rb->size - ring_buffer_available(rb) - 1;
}

bool ring_buffer_is_empty(const ring_buffer_t *rb) {
    return rb->write_pos == rb->read_pos;
}

bool ring_buffer_is_writing(const ring_buffer_t *rb) {
    return rb->is_writing;
}

void ring_buffer_end_write(ring_buffer_t *rb) {
    __sync_synchronize();
    rb->is_writing = false;
}

void ring_buffer_start_write(ring_buffer_t *rb) {
    rb->is_writing = true;
    __sync_synchronize();
}
