#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "actors/Actor.hpp"
#include "aquis/Handler.hpp"
#include "aquis/Instruments.hpp"
#include "aquis/msg/AquisBytes.hpp"

namespace aquis
{

// AquisFeed: one Aquis real-time feed (one market split of Aquis UK or Aquis
// EU) into Kaspar (not certified; see ../../../README.md).
//
//   AquisBytes (live, snapshot, replay) -> Handler -> frame::mda::msg::Data
//
// A record goes to the OB of its securityID; channel-wide records (gap, clear)
// to every OB registered; the end-of-packet record to the recorder only. The
// network side (multicast receive, replay TCP session with Login) is not here:
// it sends AquisBytes and implements `replay` (feed.md section 2).
class AquisFeed : public actors::Actor
{
public:
  struct Config
  {
    std::string name = "AquisFeed";
    HandlerConfig handler;
    std::string security_file;   // Aquis SecurityList.csv format; optional
    std::string operating_mic;   // AQXE or AQEU: rows of that venue only
    std::string universe_file;   // key,sec_id
  };

  // nullptr, with the reason in *err, when a file cannot be read.
  static std::unique_ptr<AquisFeed> create(const Config &cfg, Handler::RequestReplay replay,
                                           std::string *err);

  AquisFeed(const Config &cfg, std::unique_ptr<Instruments> ins, Handler::RequestReplay replay);

  void add_book(int32_t sec_id, actors::Actor *ob) { books_[sec_id] = ob; }
  void set_recorder(actors::Actor *rec) { recorder_ = rec; }

  const Handler &handler() const { return handler_; }
  const Instruments &instruments() const { return *ins_; }

protected:
  // Hands one record to one target; the tests override it.
  virtual void deliver(actors::Actor *target, const bfile::l3_t &r);

private:
  void bytes_handler(const msg::AquisBytes *m) noexcept;
  void emit(int32_t sec, const bfile::l3_t &r);

  std::unique_ptr<Instruments> ins_;
  Handler handler_;
  std::unordered_map<int32_t, actors::Actor *> books_;
  actors::Actor *recorder_ = nullptr;
};

} // namespace aquis
