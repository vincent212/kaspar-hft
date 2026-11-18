#include "nasdaq_md/if/NasdaqMD.hpp"

#include "interface/mcast_recv/if/SocketReader.hpp"
#include "nasdaq_md/act/NasdaqMD.hpp"

nasdaq_md::act::NasdaqMD *create_NasdaqMD(const nasdaq_md::FeedConfig &cfg,
                                          std::vector<actors::Actor *> books_by_asset,
                                          actors::Actor *binrec)
{
  return new nasdaq_md::act::NasdaqMD(cfg, std::move(books_by_asset), binrec);
}

actors::Actor *create_NasdaqMD_reader(const nasdaq_md::FeedConfig &cfg, actors::Actor *md,
                                      char feed)
{
  const std::string &group = feed == 'A' ? cfg.group_a : cfg.group_b;
  const uint16_t port = feed == 'A' ? cfg.port_a : cfg.port_b;
  if (group.empty() || !port)
    return nullptr;
  static std::string desc;
  desc = cfg.name + " MoldUDP64 " + feed;
  return create_SocketReader_64_10(cfg.name, true, feed, md, port, group.c_str(),
                                   cfg.interface.c_str(), desc.c_str(), true);
}
