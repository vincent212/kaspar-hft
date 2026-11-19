#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FrameRecorder: raw WebSocket frames to a text file, one per line:
//
//   <receive time, ns since the epoch> <frame exactly as received>
//
// Kraken's frames are single-line JSON; a newline inside a frame (never seen)
// would be written as a space. FrameReader reads the file back for replay and
// for the unit tests.

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace kraken
{
  class FrameRecorder
  {
  public:
    explicit FrameRecorder(const std::string &path) : out_(path, std::ios::out | std::ios::app) {}
    bool ok() const { return bool(out_); }
    void write(uint64_t recv_ns, std::string_view frame)
    {
      out_ << recv_ns << ' ';
      for (char c : frame)
        out_.put(c == '\n' || c == '\r' ? ' ' : c);
      out_.put('\n');
      ++frames_;
      if ((frames_ & 1023) == 0)
        out_.flush();
    }
    void flush() { out_.flush(); }
    uint64_t frames() const { return frames_; }

  private:
    std::ofstream out_;
    uint64_t frames_ = 0;
  };

  class FrameReader
  {
  public:
    explicit FrameReader(const std::string &path) : in_(path) {}
    bool ok() const { return bool(in_); }
    // false at the end of the file.
    bool next(uint64_t &recv_ns, std::string &frame)
    {
      std::string line;
      while (std::getline(in_, line))
      {
        auto sp = line.find(' ');
        if (sp == std::string::npos)
          continue;
        recv_ns = std::stoull(line.substr(0, sp));
        frame = line.substr(sp + 1);
        return true;
      }
      return false;
    }

  private:
    std::ifstream in_;
  };
}
