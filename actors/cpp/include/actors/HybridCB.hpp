/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#ifndef HYBRID_CB_HPP
#define HYBRID_CB_HPP

#include <boost/circular_buffer.hpp>
#include <boost/container/deque.hpp>
#include <cstddef>

namespace actors
{

/**
 * HybridCB - A hybrid container optimized for stack-like LIFO operations
 *
 * Combines boost::circular_buffer (fast path) with boost::container::deque (overflow).
 * Supports only back operations: push_back, pop_back, back, empty, clear.
 *
 * Design:
 * - Logical order: [overflow.front() ... overflow.back() | CB.front() ... CB.back()]
 * - Oldest elements are at overflow.front()
 * - Newest elements are at CB.back()
 *
 * Behavior:
 * - push_back when CB full: Rotate CB.front() to overflow.back(), then push to CB.back()
 * - pop_back when CB empty: Pop from overflow.back() if available
 * - No iterators - designed for memory pool bookkeeping
 * - Configurable deque block size for better cache locality during overflow
 *
 * Use case: MessageMemoryPool freeBlocks (LIFO allocation for cache locality)
 */
template <typename T, size_t DequeBlockSize = 8192>
class HybridCB {
public:
    /**
     * Constructor
     * @param capacity Initial capacity for the circular buffer (fast path)
     */
    explicit HybridCB(size_t capacity)
        : cb_(capacity)
        , overflow_()
    {}

    /**
     * Push element to back
     * If CB is full, rotates front element to overflow back before pushing
     */
    void push_back(const T& value) {
        if (cb_.full()) {
            // CB is full - rotate front element to overflow back
            T temp = cb_.front();
            cb_.pop_front();
            overflow_.push_back(temp);
            cb_.push_back(value);
        } else {
            // CB has space - direct push
            cb_.push_back(value);
        }
    }

    /**
     * Pop element from back
     * Pops from CB if available, otherwise from overflow back
     */
    void pop_back() {
        if (!cb_.empty()) {
            cb_.pop_back();
        } else if (!overflow_.empty()) {
            // Pop from overflow back (most recently rotated element)
            overflow_.pop_back();
        }
        // If both empty, this is undefined behavior (like std::vector::pop_back on empty)
    }

    /**
     * Access back element
     * @return Reference to the back element (newest)
     */
    T& back() {
        if (!cb_.empty()) {
            return cb_.back();
        } else {
            // overflow.back() has the most recently rotated element (newest in overflow)
            return overflow_.back();
        }
    }

    /**
     * Access back element (const version)
     * @return Const reference to the back element (newest)
     */
    const T& back() const {
        if (!cb_.empty()) {
            return cb_.back();
        } else {
            // overflow.back() has the most recently rotated element (newest in overflow)
            return overflow_.back();
        }
    }

    /**
     * Check if container is empty
     * @return true if both CB and overflow are empty
     */
    bool empty() const {
        return cb_.empty() && overflow_.empty();
    }

    /**
     * Clear all elements from both CB and overflow
     */
    void clear() {
        cb_.clear();
        overflow_.clear();
    }

    /**
     * Get total number of elements
     * @return Total size (CB + overflow)
     */
    size_t size() const {
        return cb_.size() + overflow_.size();
    }

    // Iterator support for range-based for loops
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        // Phase enum needs to be public so HybridCB::begin()/end() can use it
        enum Phase { PHASE_OVERFLOW, CB, END };

    private:
        typename boost::container::deque<T>::iterator overflow_it_;
        typename boost::container::deque<T>::iterator overflow_end_;
        typename boost::circular_buffer<T>::iterator cb_it_;
        typename boost::circular_buffer<T>::iterator cb_end_;
        Phase phase_;

    public:
        Iterator(typename boost::container::deque<T>::iterator overflow_it,
                typename boost::container::deque<T>::iterator overflow_end,
                typename boost::circular_buffer<T>::iterator cb_it,
                typename boost::circular_buffer<T>::iterator cb_end,
                Phase phase)
            : overflow_it_(overflow_it), overflow_end_(overflow_end),
              cb_it_(cb_it), cb_end_(cb_end), phase_(phase) {}

        T& operator*() {
            return (phase_ == PHASE_OVERFLOW) ? *overflow_it_ : *cb_it_;
        }

        Iterator& operator++() {
            if (phase_ == PHASE_OVERFLOW) {
                ++overflow_it_;
                if (overflow_it_ == overflow_end_) {
                    phase_ = (cb_it_ != cb_end_) ? CB : END;
                }
            } else if (phase_ == CB) {
                ++cb_it_;
                if (cb_it_ == cb_end_) {
                    phase_ = END;
                }
            }
            return *this;
        }

        bool operator==(const Iterator& other) const {
            if (phase_ != other.phase_) return false;
            if (phase_ == PHASE_OVERFLOW) return overflow_it_ == other.overflow_it_;
            if (phase_ == CB) return cb_it_ == other.cb_it_;
            return true;
        }

        bool operator!=(const Iterator& other) const { return !(*this == other); }
    };

    Iterator begin() {
        typename Iterator::Phase start_phase;
        if (!overflow_.empty()) {
            start_phase = Iterator::PHASE_OVERFLOW;
        } else if (!cb_.empty()) {
            start_phase = Iterator::CB;
        } else {
            start_phase = Iterator::END;
        }
        return Iterator(overflow_.begin(), overflow_.end(),
                       cb_.begin(), cb_.end(), start_phase);
    }

    Iterator end() {
        return Iterator(overflow_.end(), overflow_.end(),
                       cb_.end(), cb_.end(), Iterator::END);
    }

private:
    boost::circular_buffer<T> cb_;       // Fast path - circular buffer

    // Overflow storage with configurable block size for better cache locality
    // DequeBlockSize bytes per block (e.g., 8192 = 8KB blocks)
    using DequeType = boost::container::deque<T,
        boost::container::new_allocator<T>,
        typename boost::container::deque_options<
            boost::container::block_size<DequeBlockSize>
        >::type
    >;
    DequeType overflow_;
};

}  // namespace actors

#endif // HYBRID_CB_HPP
