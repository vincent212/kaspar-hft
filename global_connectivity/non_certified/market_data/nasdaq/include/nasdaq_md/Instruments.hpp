#pragma once
// Instrument loader (feed.md section 8): the day's Stock Directory maps each
// Stock Locate to a symbol, and the symbol to a Kaspar asset. Locates are
// per day, from 1, and not stable across days (ITCH p.3), so the map is
// rebuilt from the directory every day (or every GLIMPSE spin).
//
// Which symbols become assets, and how, is the Registrar's business: the
// dispatcher only needs the asset id (the securityID of every record), or -1
// for a security that is not routed. refdata_registrar() is the production
// one: it registers the asset in ref::RefData.
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nasdaq_md
{
struct Instrument
{
  bool known = false;          // a Stock Directory has been seen for this locate
  uint16_t locate = 0;
  std::string symbol;
  int32_t asset = -1;          // Kaspar asset id; -1 = not routed
  uint32_t tick = 100;         // the asset's tick in Price(4) units (100 = 0.01); set by the registrar
  uint32_t round_lot = 0;
  char market_category = 0;
  char authenticity = 0;       // P live, T test
  // Trading-status inputs (feed.md section 7, Trading status).
  char trading_state = 0;      // last Stock Trading Action H P Q T; 0 none yet
  bool op_halt = false;        // Operational Halt H for this feed's market
  uint8_t status = 0;          // derived CME SecurityTradingStatus
  uint8_t sent_status = 0;     // last l3_sst_t sent to the book; 0 none
};

class Instruments
{
public:
  // Returns the asset id, or -1; may set Instrument::tick.
  using Registrar = std::function<int32_t(Instrument &)>;

  explicit Instruments(Registrar r) : reg_(std::move(r)), ins_(65536) {}

  Instrument &at(uint16_t locate) { return ins_[locate]; }
  const Instrument &at(uint16_t locate) const { return ins_[locate]; }

  int32_t asset(uint16_t locate) const { return ins_[locate].asset; }

  // A Stock Directory message (ITCH R, or GLIMPSE R). Returns true when the
  // locate was new. A repeated directory for the same locate (a GLIMPSE spin
  // after the start of day) refreshes the fields and keeps the asset.
  bool on_directory(uint16_t locate, std::string_view symbol, uint32_t round_lot,
                    char market_category, char authenticity)
  {
    auto &i = ins_[locate];
    const bool fresh = !i.known || i.symbol != symbol;
    i.known = true;
    i.locate = locate;
    i.symbol = std::string(symbol);
    i.round_lot = round_lot;
    i.market_category = market_category;
    i.authenticity = authenticity;
    if (fresh)
    {
      i.asset = reg_ ? reg_(i) : -1;
      if (i.asset >= 0)
        routed_.push_back(locate);
    }
    return fresh;
  }

  // Locates whose securities are routed to a book, in directory order.
  const std::vector<uint16_t> &routed() const { return routed_; }

  // A new day: locates are reassigned.
  void clear()
  {
    for (auto &i : ins_)
      i = Instrument{};
    routed_.clear();
  }

  // Forget the trading-status inputs (a GLIMPSE recovery resends them).
  void clear_status()
  {
    for (auto &i : ins_)
    {
      i.trading_state = 0;
      i.op_halt = false;
    }
  }

private:
  Registrar reg_;
  std::vector<Instrument> ins_;
  std::vector<uint16_t> routed_;
};

// Production registrar: registers each selected live symbol with
// ref::RefData::add_future_asset (the dynamic path the other handlers use) as
// `<prefix><symbol>`, with maxpx above the largest Price(4) (200,000.0000,
// ITCH p.4). The asset's sec_id is unique per venue and locate
// (sec_id_base + locate); records carry the asset id.
//
// The tick is not in the feed (feed.md section 8): it is ticks[symbol] if
// given, else default_tick. 0.01 suits stocks quoted in pennies; a stock that
// is quoted below $1 (sub-penny) needs 0.0001, or OB rounds its prices onto
// pennies. The dispatcher counts prices that are not on the tick
// (DispatcherStats::off_tick). OB's price ladder must be a ring (ladder_ticks)
// with either tick, since maxpx is 2e7 or 2e9 ticks.
//
// symbols empty = every live symbol. Test issues (Authenticity T, ITCH p.6) are
// skipped unless include_test. The registrar sets Instrument::tick.
Instruments::Registrar refdata_registrar(char venue, std::set<std::string> symbols,
                                         bool include_test, std::string prefix,
                                         int32_t sec_id_base, double default_tick = 0.01,
                                         std::map<std::string, double> ticks = {});
} // namespace nasdaq_md
