#pragma once
// Factory for the Nasdaq TotalView-ITCH 5.0 handler (not certified), in the
// style of interface/<lib>/if/*.hpp. It lives in the venue directory because
// the handler is not part of the core build.
//
//   auto cfg = nasdaq_md::config_from_ini(pt.get_child("NASDAQ"));
//   auto md  = create_NasdaqMD(cfg, books_by_asset, binrec);
//   auto a   = create_NasdaqMD_reader(cfg, md, 'A');   // SocketReader<uint64_t,10>
//   auto b   = create_NasdaqMD_reader(cfg, md, 'B');
//   manager->manage(md); manager->manage(a); manager->manage(b);
#include "actors/Actor.hpp"
#include "nasdaq_md/Config.hpp"

#include <vector>

namespace nasdaq_md::act
{
class NasdaqMD;
}

nasdaq_md::act::NasdaqMD *create_NasdaqMD(const nasdaq_md::FeedConfig &cfg,
                                          std::vector<actors::Actor *> books_by_asset,
                                          actors::Actor *binrec);

// The multicast receiver of feed A or B, sending to md: mcast_recv's
// SocketReader<uint64_t, 10> (the 8-byte MoldUDP64 sequence after the 10-byte
// session, big-endian). Returns nullptr if the feed has no group configured.
actors::Actor *create_NasdaqMD_reader(const nasdaq_md::FeedConfig &cfg, actors::Actor *md,
                                      char feed);
