#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FrameRecorder / FrameReader: raw WebSocket frames, exactly as received, with
// the local receive time, for replay into the sync class and for unit tests.
//
// File format, little-endian, one record per frame:
//   uint64 recv_ns   local wall clock, ns since the epoch
//   uint32 len       bytes of the frame
//   char   data[len] the frame's text as received (no terminator)
//
// The recorder is opened in append mode and flushes after every frame, so a
// killed process loses at most the frame being written; the reader stops at
// a truncated last record.

#include <cstdint>
#include <cstdio>
#include <string>

namespace deribit
{
  class FrameRecorder
  {
  public:
    explicit FrameRecorder(const std::string &path) : f_(std::fopen(path.c_str(), "ab")) {}
    ~FrameRecorder()
    {
      if (f_)
        std::fclose(f_);
    }
    FrameRecorder(const FrameRecorder &) = delete;
    FrameRecorder &operator=(const FrameRecorder &) = delete;

    bool ok() const { return f_ != nullptr; }
    uint64_t frames() const { return n_; }

    void write(uint64_t recv_ns, const std::string &frame)
    {
      if (!f_)
        return;
      const uint32_t len = uint32_t(frame.size());
      std::fwrite(&recv_ns, 8, 1, f_);
      std::fwrite(&len, 4, 1, f_);
      std::fwrite(frame.data(), 1, len, f_);
      std::fflush(f_);
      ++n_;
    }

  private:
    std::FILE *f_;
    uint64_t n_ = 0;
  };

  class FrameReader
  {
  public:
    explicit FrameReader(const std::string &path) : f_(std::fopen(path.c_str(), "rb")) {}
    ~FrameReader()
    {
      if (f_)
        std::fclose(f_);
    }
    FrameReader(const FrameReader &) = delete;
    FrameReader &operator=(const FrameReader &) = delete;

    bool ok() const { return f_ != nullptr; }

    // The next frame; false at the end of the file or at a truncated record.
    bool next(uint64_t &recv_ns, std::string &frame)
    {
      uint32_t len = 0;
      if (!f_ || std::fread(&recv_ns, 8, 1, f_) != 1 || std::fread(&len, 4, 1, f_) != 1)
        return false;
      frame.resize(len);
      return std::fread(frame.data(), 1, len, f_) == len;
    }

  private:
    std::FILE *f_;
  };
}
