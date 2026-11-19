#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Factories for the Japannext market-data actors (the interface/<lib>/if/
// headers of the other handlers; kept here while the handler is not certified).
//
// One [jnx.<stream>] section of config/jnx_example.ini per MoldUDP64 stream:
//
//   create_all_jnx(section, book_for, binrec)
//     -> the JnxFeed, its GlimpseClient and an mcast_recv SocketReader<uint64_t, 10>
//        that sends every datagram of the stream's multicast group to the feed.
//
// The caller adds the returned actors to its Manager (the SocketReader and the
// GlimpseClient each need a thread of their own; the feed may share the books'
// group when fast_send is on).

#include <string>
#include <vector>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "jnx_md/act/GlimpseClient.hpp"
#include "jnx_md/act/JnxFeed.hpp"

namespace jnx_md
{
  // The feed's settings from an ini section; throws on a missing or bad value.
  act::FeedConfig feed_config(const boost::property_tree::ptree &section);
  // The GLIMPSE settings; the username and password come from the file named
  // by `credentials` (two lines), kept outside the repository.
  act::GlimpseConfig glimpse_config(const boost::property_tree::ptree &section);
}

actor_ptr create_JnxFeed(const boost::property_tree::ptree &section,
                         jnx_md::act::JnxFeed::BookFor book_for, actor_ptr binrec);

actor_ptr create_JnxGlimpseClient(const boost::property_tree::ptree &section, actor_ptr feed);

// Feed, GLIMPSE client and multicast reader for one stream, wired together.
std::vector<actor_ptr> create_all_jnx(const boost::property_tree::ptree &section,
                                      jnx_md::act::JnxFeed::BookFor book_for,
                                      actor_ptr binrec);
