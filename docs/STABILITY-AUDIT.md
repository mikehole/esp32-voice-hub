# ESP32 Voice Hub - Stability Audit

Based on research into ESP32-S3 best practices, FreeRTOS dual-core patterns, LVGL thread safety, and common crash patterns.

## Current Issues Identified

### 1. ❌ LVGL Mutex Not Global

**Problem:** `status_ring.cpp` has a local mutex, but other files (`main.cpp`, `avatar.cpp`) call LVGL directly without any mutex protection.

**Impact:** Race conditions between `lv_timer_handler()` in loop() and any LVGL calls from other tasks/contexts.

**Files affected:**
- `main.cpp` - calls `lv_timer_handler()`, `lv_task_handler()`, various `lv_*` functions
- `avatar.cpp` - calls `lv_img_create()`, `lv_img_set_src()`, etc.
- `status_ring.cpp` - has local mutex, not shared

**Fix:** Create a global LVGL mutex in a dedicated `lvgl_port.cpp/h` that ALL LVGL callers use.

---

### 2. ❌ Audio Task Accesses Shared Variables Unsafely

**Problem:** The new audio playback task on Core 1 writes to `current_audio_level` while the UI on Core 0 reads it. This is a data race.

**Impact:** Potential for torn reads or undefined behavior.

**Fix:** Use `volatile` or atomic operations for `current_audio_level`, or accept the benign race (display glitch at worst).

---

### 3. ⚠️ WiFi + LVGL Conflict Potential

**Problem:** WiFi runs on Core 0. The Arduino `loop()` (which calls `lv_timer_handler()`) runs on Core 1. But web server handlers run on Core 0 and might trigger LVGL updates indirectly (e.g., via `notification_queue()`).

**Impact:** If web handlers update state that UI reads, or vice versa, race conditions occur.

**Current mitigation:** Most state updates are simple flag writes (atomic on ESP32).

---

### 4. ❌ No Task Stack Size Verification

**Problem:** Several tasks are created with arbitrary stack sizes. Stack overflow causes heap corruption.

**Current tasks:**
- `recording_task` - 4096 bytes
- `playback_task` - 4096 bytes  
- Voice processing task - ?

**Fix:** Use `uxTaskGetStackHighWaterMark()` to measure actual usage and size appropriately.

---

### 5. ⚠️ Memory Allocation in Critical Paths

**Problem:** Audio playback allocates during operation:
```cpp
// In playback_task:
int16_t* vol_buf = (int16_t*)heap_caps_malloc(CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

**Impact:** Heap fragmentation, potential allocation failure during playback.

**Fix:** Pre-allocate buffers at init time, or use static buffers.

---

### 6. ❌ WiFi Connection Has No Timeout Handling

**Problem:** Looking at the boot log, WiFi connection attempts can stall:
```
WiFi connecting...
WiFi: Got IP: 192.16  (truncated?)
```

**Impact:** UI freezes during WiFi operations.

**Fix:** Move WiFi connection to a dedicated task, with timeout and retry logic.

---

### 7. ⚠️ Watchdog Not Fed During Long Operations

**Problem:** During long audio playback or TTS generation, the watchdog might not be fed.

**Impact:** Unexpected resets.

**Fix:** Add `esp_task_wdt_reset()` calls in long-running loops.

---

## Recommended Architecture Changes

### Global LVGL Mutex

Create `lvgl_port.h/cpp`:

```cpp
// lvgl_port.h
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

void lvgl_port_init();
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock();
```

```cpp
// lvgl_port.cpp
#include "lvgl_port.h"

static SemaphoreHandle_t lvgl_mutex = NULL;

void lvgl_port_init() {
    lvgl_mutex = xSemaphoreCreateMutex();
}

bool lvgl_port_lock(uint32_t timeout_ms) {
    if (!lvgl_mutex) return false;
    return xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock() {
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}
```

### LVGL Task (Instead of loop())

Move LVGL handling to a dedicated task:

```cpp
void lvgl_task(void* param) {
    while (1) {
        if (lvgl_port_lock(100)) {
            uint32_t delay = lv_timer_handler();
            lvgl_port_unlock();
            
            if (delay > 50) delay = 50;
            if (delay < 5) delay = 5;
            vTaskDelay(pdMS_TO_TICKS(delay));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
```

Pin to Core 1 with priority ~15.

### WiFi Task

Move WiFi to dedicated task on Core 0:

```cpp
void wifi_task(void* param) {
    wifi_init();
    
    while (1) {
        if (WiFi.status() != WL_CONNECTED) {
            wifi_connect_with_retry();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Priority Table (Proposed)

| Task | Core | Priority | Stack |
|------|------|----------|-------|
| WiFi (system) | 0 | system | system |
| Web server handlers | 0 | system | system |
| Audio playback | 1 | 12 | 4096 |
| Audio recording | 0 | 10 | 4096 |
| LVGL handler | 1 | 8 | 8192 |
| Main logic | 1 | 5 | 8192 |
| Idle | both | 0 | minimal |

---

## Quick Wins (Low Risk)

1. **Make `current_audio_level` volatile**
2. **Add heap logging every 10s** (already done ✓)
3. **Pre-allocate audio volume buffer at init**
4. **Add stack high water mark logging**

## Medium Effort (Recommended)

1. **Create global LVGL mutex**
2. **Have all LVGL callers use the mutex**
3. **Move LVGL handler to dedicated task**

## High Effort (Future)

1. **Full WiFi task isolation**
2. **Proper event-driven architecture with queues**
3. **Comprehensive watchdog management**

---

## Testing Checklist

After changes:

- [ ] Device boots reliably 10/10 times
- [ ] WiFi connects within 10 seconds
- [ ] Notification audio plays without crash
- [ ] Rapid tap doesn't crash
- [ ] 24-hour uptime test passes
- [ ] No heap growth over time (memory leak check)
