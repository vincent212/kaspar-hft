#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <boost/circular_buffer.hpp>
#include <boost/container/deque.hpp>

namespace actors
{
  // HybridBuffer: Circular buffer with dual deque overflow for graceful degradation
  // Provides std::deque-like interface with high-performance overflow handling
  //
  // Design:
  // - Fast path: Uses fixed-capacity circular buffer (zero malloc overhead)
  // - Overflow: Two separate boost::container::deques for front and back overflow
  // - Logical order: [overflow_front_ ... | cb_ ... | ... overflow_back_]
  // - FIFO order: Strictly maintained across all three containers
  //
  // Performance characteristics:
  // - Common case (no overflow): O(1) operations, zero malloc, excellent cache locality
  // - Overflow case: O(1) operations, no migration cost, configurable deque block size
  // - Configurable deque block size (default 8KB) for better cache locality during overflow
  //
  // Usage:
  //   HybridBuffer<int> buffer{8192};  // Circular buffer capacity: 8192
  //   buffer.push_back(42);
  //   int val = buffer.front();
  //   buffer.pop_front();
  //
  template <typename T, size_t DequeBlockSize = 8192>
  class HybridBuffer
  {
  private:
    using DequeType = boost::container::deque<T,
        boost::container::new_allocator<T>,
        typename boost::container::deque_options<
            boost::container::block_size<DequeBlockSize>
        >::type
    >;

    boost::circular_buffer<T> cb_;           // Fast path - fixed capacity, zero malloc
    DequeType overflow_front_;               // Overflow for push_front when CB full
    DequeType overflow_back_;                // Overflow for push_back when CB full

  public:
    explicit HybridBuffer(size_t capacity) : cb_(capacity) {}

    // Check if all containers are empty
    bool empty() const {
      return cb_.empty() && overflow_front_.empty() && overflow_back_.empty();
    }

    // Get front element
    // Order: overflow_front_ first, then CB, then overflow_back_
    T front() const {
      if (!overflow_front_.empty()) {
        return overflow_front_.front();
      }
      if (!cb_.empty()) {
        return cb_.front();
      }
      return overflow_back_.front();
    }

    // Remove front element
    void pop_front() {
      if (!overflow_front_.empty()) {
        overflow_front_.pop_front();
      } else if (!cb_.empty()) {
        cb_.pop_front();
      } else {
        overflow_back_.pop_front();
      }
    }

    // Get back element
    // Order: overflow_back_ first, then CB, then overflow_front_
    T back() const {
      if (!overflow_back_.empty()) {
        return overflow_back_.back();
      }
      if (!cb_.empty()) {
        return cb_.back();
      }
      return overflow_front_.back();
    }

    // Add element to back
    void push_back(const T& value) {
      if (!overflow_front_.empty() || !overflow_back_.empty()) {
        // Already in overflow mode - use back overflow
        overflow_back_.push_back(value);
      } else if (cb_.full()) {
        // CB is full - start using overflow_back_ (no migration!)
        overflow_back_.push_back(value);
      } else {
        // Fast path: use circular buffer
        cb_.push_back(value);
      }
    }

    // Remove back element
    void pop_back() {
      if (!overflow_back_.empty()) {
        overflow_back_.pop_back();
      } else if (!cb_.empty()) {
        cb_.pop_back();
      } else {
        overflow_front_.pop_back();
      }
    }

    // Add element to front
    void push_front(const T& value) {
      if (!overflow_front_.empty() || !overflow_back_.empty()) {
        // Already in overflow mode - use front overflow
        overflow_front_.push_front(value);
      } else if (cb_.full()) {
        // CB is full - start using overflow_front_ (no migration!)
        overflow_front_.push_front(value);
      } else {
        // Fast path: use circular buffer
        cb_.push_front(value);
      }
    }

    // Clear all containers
    void clear() {
      cb_.clear();
      overflow_front_.clear();
      overflow_back_.clear();
    }

    // Iterator support for range-based for loops
    // Iterates: overflow_front_, then CB, then overflow_back_
    class Iterator {
    public:
      enum Phase { FRONT_OVERFLOW, CB, BACK_OVERFLOW, END };

    private:
      typename DequeType::iterator overflow_front_begin_;
      typename DequeType::iterator overflow_front_it_;
      typename DequeType::iterator overflow_front_end_;
      typename boost::circular_buffer<T>::iterator cb_begin_;
      typename boost::circular_buffer<T>::iterator cb_it_;
      typename boost::circular_buffer<T>::iterator cb_end_;
      typename DequeType::iterator overflow_back_begin_;
      typename DequeType::iterator overflow_back_it_;
      typename DequeType::iterator overflow_back_end_;

      Phase phase_;

    public:
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type = T;
      using difference_type = std::ptrdiff_t;
      using pointer = T*;
      using reference = T&;

      Iterator(typename DequeType::iterator overflow_front_begin,
               typename DequeType::iterator overflow_front_it,
               typename DequeType::iterator overflow_front_end,
               typename boost::circular_buffer<T>::iterator cb_begin,
               typename boost::circular_buffer<T>::iterator cb_it,
               typename boost::circular_buffer<T>::iterator cb_end,
               typename DequeType::iterator overflow_back_begin,
               typename DequeType::iterator overflow_back_it,
               typename DequeType::iterator overflow_back_end,
               Phase phase)
        : overflow_front_begin_(overflow_front_begin), overflow_front_it_(overflow_front_it), overflow_front_end_(overflow_front_end),
          cb_begin_(cb_begin), cb_it_(cb_it), cb_end_(cb_end),
          overflow_back_begin_(overflow_back_begin), overflow_back_it_(overflow_back_it), overflow_back_end_(overflow_back_end),
          phase_(phase) {}

      T& operator*() {
        if (phase_ == FRONT_OVERFLOW) return *overflow_front_it_;
        if (phase_ == CB) return *cb_it_;
        return *overflow_back_it_;
      }

      T* operator->() {
        if (phase_ == FRONT_OVERFLOW) return &(*overflow_front_it_);
        if (phase_ == CB) return &(*cb_it_);
        return &(*overflow_back_it_);
      }

      Iterator& operator++() {
        if (phase_ == FRONT_OVERFLOW) {
          ++overflow_front_it_;
          if (overflow_front_it_ == overflow_front_end_) {
            // Finished front overflow - check if CB has elements
            if (cb_it_ != cb_end_) {
              phase_ = CB;
            } else if (overflow_back_it_ != overflow_back_end_) {
              phase_ = BACK_OVERFLOW;
            } else {
              phase_ = END;
            }
          }
        } else if (phase_ == CB) {
          ++cb_it_;
          if (cb_it_ == cb_end_) {
            // Finished CB - check if back overflow has elements
            if (overflow_back_it_ != overflow_back_end_) {
              phase_ = BACK_OVERFLOW;
            } else {
              phase_ = END;
            }
          }
        } else if (phase_ == BACK_OVERFLOW) {
          ++overflow_back_it_;
          if (overflow_back_it_ == overflow_back_end_) {
            phase_ = END;
          }
        }
        return *this;
      }

      Iterator& operator--() {
        if (phase_ == END) {
          // Coming back from END - start at last element
          if (overflow_back_it_ != overflow_back_begin_) {
            phase_ = BACK_OVERFLOW;
            overflow_back_it_ = overflow_back_end_;
            --overflow_back_it_;
          } else if (cb_it_ != cb_begin_) {
            phase_ = CB;
            cb_it_ = cb_end_;
            --cb_it_;
          } else if (overflow_front_it_ != overflow_front_begin_) {
            phase_ = FRONT_OVERFLOW;
            overflow_front_it_ = overflow_front_end_;
            --overflow_front_it_;
          }
        } else if (phase_ == BACK_OVERFLOW) {
          if (overflow_back_it_ == overflow_back_begin_) {
            // At beginning of back overflow - move to CB or front overflow
            if (cb_it_ != cb_begin_) {
              phase_ = CB;
              cb_it_ = cb_end_;
              --cb_it_;
            } else if (overflow_front_it_ != overflow_front_begin_) {
              phase_ = FRONT_OVERFLOW;
              overflow_front_it_ = overflow_front_end_;
              --overflow_front_it_;
            }
          } else {
            --overflow_back_it_;
          }
        } else if (phase_ == CB) {
          if (cb_it_ == cb_begin_) {
            // At beginning of CB - move to front overflow
            if (overflow_front_it_ != overflow_front_begin_) {
              phase_ = FRONT_OVERFLOW;
              overflow_front_it_ = overflow_front_end_;
              --overflow_front_it_;
            }
          } else {
            --cb_it_;
          }
        } else if (phase_ == FRONT_OVERFLOW) {
          --overflow_front_it_;
        }
        return *this;
      }

      bool operator==(const Iterator& other) const {
        if (phase_ != other.phase_) return false;
        if (phase_ == FRONT_OVERFLOW) return overflow_front_it_ == other.overflow_front_it_;
        if (phase_ == CB) return cb_it_ == other.cb_it_;
        if (phase_ == BACK_OVERFLOW) return overflow_back_it_ == other.overflow_back_it_;
        return true; // Both at END
      }

      bool operator!=(const Iterator& other) const { return !(*this == other); }
    };

    Iterator begin() {
      // Determine starting phase
      typename Iterator::Phase start_phase;
      if (!overflow_front_.empty()) {
        start_phase = Iterator::FRONT_OVERFLOW;
      } else if (!cb_.empty()) {
        start_phase = Iterator::CB;
      } else if (!overflow_back_.empty()) {
        start_phase = Iterator::BACK_OVERFLOW;
      } else {
        start_phase = Iterator::END;
      }

      return Iterator(overflow_front_.begin(), overflow_front_.begin(), overflow_front_.end(),
                     cb_.begin(), cb_.begin(), cb_.end(),
                     overflow_back_.begin(), overflow_back_.begin(), overflow_back_.end(),
                     start_phase);
    }

    Iterator end() {
      return Iterator(overflow_front_.begin(), overflow_front_.end(), overflow_front_.end(),
                     cb_.begin(), cb_.end(), cb_.end(),
                     overflow_back_.begin(), overflow_back_.end(), overflow_back_.end(),
                     Iterator::END);
    }

    // Reverse iterators using std::reverse_iterator
    using reverse_iterator = std::reverse_iterator<Iterator>;

    reverse_iterator rbegin() {
      return reverse_iterator(end());
    }

    reverse_iterator rend() {
      return reverse_iterator(begin());
    }
  };

}  // namespace actors
