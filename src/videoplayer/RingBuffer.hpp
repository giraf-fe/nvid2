#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <concepts>

template <typename T, size_t Capacity, bool StartFull = false>
class RingBuffer {
    size_t pushHead = 0;
    size_t readTail = 0;
    size_t count = 0;
public:
    std::array<T, Capacity> buffer{};
    
    RingBuffer() {
        if constexpr (StartFull) {
            count = Capacity;
        }
    }
    inline bool empty() const {
        return count == 0;
    }
    inline bool full() const {
        return count == Capacity;
    }
    inline size_t size() const {
        return count;
    }
    inline size_t capacity() const {
        return Capacity;
    }
    bool push(const T& item) {
        if (full()) { return false; }
        buffer[pushHead] = item;
        pushHead = (pushHead  + 1) % Capacity;
        count++;
        return true;
    }
    T& pop(bool& success) {
        if (empty()) { success = false; return buffer[0]; }
        T& item = buffer[readTail];
        readTail = (readTail + 1) % Capacity;
        count--;
        success = true;
        return item;
    }
};

template <typename FrameBuffer, size_t Count>
class SwapChain {
    std::array<FrameBuffer, Count> buffers{};
    RingBuffer<size_t, Count, true> availableIndices;
    
public:
    SwapChain() {
        // Initialize with all buffer indices available
        for (size_t i = 0; i < Count; i++) {
            availableIndices.buffer[i] = i;
        }
    }
    
    // Acquire a buffer for rendering/writing
    FrameBuffer* acquire() {
        bool success;
        size_t index = availableIndices.pop(success);
        if (!success) {
            return nullptr;
        }
        return &buffers[index];
    }
    
    // Present/release a buffer back to the available pool
    bool release(FrameBuffer* buffer) {
        if (!buffer) return false;
        
        // Calculate index from pointer
        size_t index = buffer - buffers.data();
        if (index >= Count) {
            return false;
        }
        
        return availableIndices.push(index);
    }
    
    // Get buffer by index (for direct access if needed)
    FrameBuffer& operator[](size_t index) {
        return buffers[index];
    }
    
    const FrameBuffer& operator[](size_t index) const {
        return buffers[index];
    }
    
    size_t availableCount() const {
        return availableIndices.size();
    }
    
    constexpr size_t capacity() const {
        return Count;
    }
};
