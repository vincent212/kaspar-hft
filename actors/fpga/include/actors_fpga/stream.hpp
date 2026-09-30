#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * hls::stream -- the FIFO that carries messages between processes on the FPGA.
 *
 * Under Vitis HLS (synthesis, or with -DKASPAR_VITIS) this is AMD's
 * <hls_stream.h>. Otherwise it is a small host-side stand-in with the same
 * interface, so the runtime's unit tests build with plain g++/clang++.
 */

#if defined(__SYNTHESIS__) || defined(KASPAR_VITIS)
#include <hls_stream.h>
#else
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>

namespace hls {

// Host stand-in. Thread-safe, and read() blocks until data arrives, like a
// hardware FIFO, so a design can run with one thread per process.
template <class T>
class stream
{
public:
  stream() = default;
  explicit stream(const char *) {}
  stream(const stream &) = delete;
  stream &operator=(const stream &) = delete;

  bool empty() const
  {
    std::lock_guard<std::mutex> lk(m_);
    return q_.empty();
  }
  bool full() const { return false; }
  std::size_t size() const
  {
    std::lock_guard<std::mutex> lk(m_);
    return q_.size();
  }

  T read()
  {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [this] { return !q_.empty(); });
    T v = q_.front();
    q_.pop_front();
    return v;
  }
  void read(T &v) { v = read(); }
  bool read_nb(T &v)
  {
    std::lock_guard<std::mutex> lk(m_);
    if (q_.empty())
      return false;
    v = q_.front();
    q_.pop_front();
    return true;
  }
  void write(const T &v)
  {
    {
      std::lock_guard<std::mutex> lk(m_);
      q_.push_back(v);
    }
    cv_.notify_one();
  }
  bool write_nb(const T &v)
  {
    write(v);
    return true;
  }

private:
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::deque<T> q_;
};

} // namespace hls
#endif
