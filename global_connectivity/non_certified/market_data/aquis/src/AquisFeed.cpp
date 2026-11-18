/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "aquis/act/AquisFeed.hpp"

#include <cstdio>

#include "frame/mda/msg/Data.hpp"

namespace aquis
{

std::unique_ptr<AquisFeed> AquisFeed::create(const Config &cfg, Handler::RequestReplay replay,
                                             std::string *err)
{
  auto ins = std::make_unique<Instruments>();
  if (!cfg.universe_file.empty() && ins->load_universe(cfg.universe_file, err) < 0)
    return nullptr;
  if (!cfg.security_file.empty() && ins->load_security_file(cfg.security_file, cfg.operating_mic, err) < 0)
    return nullptr;
  return std::make_unique<AquisFeed>(cfg, std::move(ins), std::move(replay));
}

AquisFeed::AquisFeed(const Config &cfg, std::unique_ptr<Instruments> ins, Handler::RequestReplay replay)
    : ins_(std::move(ins)),
      handler_(cfg.handler, *ins_, [this](int32_t sec, const bfile::l3_t &r) { emit(sec, r); },
               std::move(replay))
{
  snprintf(name, sizeof(name), "%s", cfg.name.c_str());
  MESSAGE_HANDLER(msg::AquisBytes, bytes_handler);
}

void AquisFeed::bytes_handler(const msg::AquisBytes *m) noexcept
{
  switch (m->source)
  {
  case msg::AquisBytes::Source::live:
    handler_.on_packet(m->data.data(), m->data.size());
    break;
  case msg::AquisBytes::Source::snapshot:
    handler_.on_snapshot_packet(m->data.data(), m->data.size());
    break;
  case msg::AquisBytes::Source::replay:
    handler_.on_replay_data(m->data.data(), m->data.size());
    break;
  case msg::AquisBytes::Source::replay_lost:
    handler_.on_replay_lost();
    break;
  }
}

void AquisFeed::emit(int32_t sec, const bfile::l3_t &r)
{
  if (sec == Handler::RECORDER)
  {
    if (recorder_)
      deliver(recorder_, r);
    return;
  }
  if (sec == Handler::ALL_BOOKS)
  {
    for (auto &[id, ob] : books_)
      deliver(ob, r);
    if (recorder_)
      deliver(recorder_, r);
    return;
  }
  if (auto it = books_.find(sec); it != books_.end())
    deliver(it->second, r);
  if (recorder_)
    deliver(recorder_, r);
}

void AquisFeed::deliver(actors::Actor *target, const bfile::l3_t &r)
{
  auto *m = new frame::mda::msg::Data(true);
  m->l3 = r;
  target->send(m, this);
}

} // namespace aquis
