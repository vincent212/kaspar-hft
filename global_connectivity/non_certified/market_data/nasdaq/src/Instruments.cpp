#include "nasdaq_md/Instruments.hpp"

#include "enum/e_names.hpp"
#include "frame/ref/RefData.hpp"

namespace nasdaq_md
{
Instruments::Registrar refdata_registrar(char venue, std::set<std::string> symbols,
                                         bool include_test, std::string prefix,
                                         int32_t sec_id_base, double default_tick,
                                         std::map<std::string, double> ticks)
{
  return [venue, symbols = std::move(symbols), include_test, prefix = std::move(prefix),
          sec_id_base, default_tick, ticks = std::move(ticks)](Instrument &i) -> int32_t {
    if (i.authenticity != 'P' && !include_test)
      return -1;
    if (!symbols.empty() && !symbols.count(i.symbol))
      return -1;
    auto t = ticks.find(i.symbol);
    const double tick = t != ticks.end() ? t->second : default_tick;
    frame::ref::RefData::inst();   // add_future_asset does not construct RefData itself
    auto *a = frame::ref::RefData::add_future_asset(prefix + i.symbol, en::x(uint8_t(venue)),
                                                    sec_id_base + int32_t(i.locate), "", "", tick);
    if (!a)
      return -1;
    // Largest Price(4) is 200,000.0000 (ITCH p.4).
    a->maxpx = int(200000.0 / tick + 1.5);
    i.tick = uint32_t(tick * 10000.0 + 0.5);
    return int32_t(a->id);
  };
}
} // namespace nasdaq_md
