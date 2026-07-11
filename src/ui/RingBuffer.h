#pragma once
#include <array>
#include <cstddef>

namespace ui {

// Fixed-capacity ring buffer for the 60-second rolling graphs. Mirrors the
// well-known "ScrollingBuffer" idiom from imgui/implot demo code: push is O(1),
// and ForEach walks the data in chronological order without shifting memory.
template <typename T, size_t N>
class RingBuffer {
public:
    void Push(T value) {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (count_ < N) {
            ++count_;
        }
    }

    size_t Size() const { return count_; }

    // index 0 is the oldest sample, Size()-1 is the newest.
    T operator[](size_t index) const {
        size_t start = (count_ < N) ? 0 : head_;
        return data_[(start + index) % N];
    }

private:
    std::array<T, N> data_{};
    size_t head_ = 0;
    size_t count_ = 0;
};

} // namespace ui
