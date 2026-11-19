/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/if/JnxFeed.hpp"

#include <fstream>
#include <stdexcept>

#include "mcast_recv/if/SocketReader.hpp"

namespace jnx_md
{
  act::FeedConfig feed_config(const boost::property_tree::ptree &s)
  {
    act::FeedConfig c;
    c.name = s.get<std::string>("name", "JNX");
    auto &h = c.handler;
    const unsigned stream = s.get<unsigned>("stream", 0);
    if (stream > 63)
      throw std::runtime_error("jnx: stream must be 0..63 (orderID bits 58..63)");
    h.dispatch.stream = stream;
    // Empty: from the clock at each new session (Handler::Hooks::session_midnight).
    if (const std::string d = s.get<std::string>("session_date", ""); !d.empty())
      h.dispatch.midnight_ns = jst_midnight_ns(std::stoi(d));
    const std::string join = s.get<std::string>("join", "snapshot");
    if (join == "snapshot")
      h.join = Handler::Join::Snapshot;
    else if (join == "one")
      h.join = Handler::Join::FromOne;
    else if (join == "first")
      h.join = Handler::Join::FirstPacket;
    else
      throw std::runtime_error("jnx: join must be snapshot, one or first");
    h.rerequest_timeout_ns = uint64_t(s.get<double>("rerequest_timeout_s", 2.0) * 1e9);
    h.glimpse_retry_ns = uint64_t(s.get<double>("glimpse_retry_s", 1.0) * 1e9);
    h.glimpse_retry_max_ns = uint64_t(s.get<double>("glimpse_retry_max_s", 30.0) * 1e9);
    h.instruments.secid_base = s.get<int32_t>("secid_base", 81000000 + int32_t(stream) * 100000);
    h.instruments.name_prefix = s.get<std::string>("asset_prefix", "JNX:");
    c.rerequest_host = s.get<std::string>("rerequest_host", "");
    c.rerequest_port = uint16_t(s.get<unsigned>("rerequest_port", 0));
    c.fast_send = s.get<bool>("fast_send", true);
    return c;
  }

  act::GlimpseConfig glimpse_config(const boost::property_tree::ptree &s)
  {
    act::GlimpseConfig g;
    g.name = s.get<std::string>("name", "JNX");
    g.host = s.get<std::string>("glimpse_host", "");
    g.port = uint16_t(s.get<unsigned>("glimpse_port", 0));
    const std::string cred = s.get<std::string>("credentials", "");
    if (!cred.empty())
    {
      std::ifstream f(cred);
      if (!f || !std::getline(f, g.username) || !std::getline(f, g.password))
        throw std::runtime_error("jnx: cannot read username and password from " + cred);
    }
    return g;
  }
}

actor_ptr create_JnxFeed(const boost::property_tree::ptree &section,
                         jnx_md::act::JnxFeed::BookFor book_for, actor_ptr binrec)
{
  return new jnx_md::act::JnxFeed(jnx_md::feed_config(section), std::move(book_for), binrec);
}

actor_ptr create_JnxGlimpseClient(const boost::property_tree::ptree &section, actor_ptr feed)
{
  auto *g = new jnx_md::act::GlimpseClient(jnx_md::glimpse_config(section), feed);
  if (auto *f = dynamic_cast<jnx_md::act::JnxFeed *>(feed))
    f->set_glimpse(g);
  return g;
}

std::vector<actor_ptr> create_all_jnx(const boost::property_tree::ptree &section,
                                      jnx_md::act::JnxFeed::BookFor book_for, actor_ptr binrec)
{
  auto *feed = create_JnxFeed(section, std::move(book_for), binrec);
  auto *glimpse = create_JnxGlimpseClient(section, feed);
  // MoldUDP64: the 64-bit big-endian sequence number is at offset 10 (MOLD 3.1).
  const std::string name = section.get<std::string>("name", "JNX");
  const std::string group = section.get<std::string>("group");
  const std::string interface = section.get<std::string>("interface", "0.0.0.0");
  const std::string desc = name + " MoldUDP64";
  auto *reader = create_SocketReader_64_10(name, true, 'A', feed,
                                           in_port_t(section.get<unsigned>("port")),
                                           group.c_str(), interface.c_str(), desc.c_str(), true);
  return {reader, feed, glimpse};
}
