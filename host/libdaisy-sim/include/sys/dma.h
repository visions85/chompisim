#pragma once
#include <cstddef>
#include <cstdint>
/** Cache maintenance helpers are no-ops on the host. */
static inline void dsy_dma_init(void) {}
static inline void dsy_dma_deinit(void) {}
static inline void dsy_dma_clear_cache_for_buffer(uint8_t* buffer, size_t size) { (void)buffer; (void)size; }
static inline void dsy_dma_invalidate_cache_for_buffer(uint8_t* buffer, size_t size) { (void)buffer; (void)size; }
