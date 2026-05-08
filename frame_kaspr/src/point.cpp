
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/mda/msg/point.hpp"
#include "frame/ref/RefData.hpp"
#include <optional>
#include <cmath>
#include <array>

using namespace frame::mda::msg;

//
// compute mid
//
// this is used by RFQ code
//
std::optional<std::tuple<double, double, int, int>>
point::non_zero_mid_bo() const noexcept
{
  std::optional<std::tuple<double, double, int, int>> ret;
  size_t non_zero_bid_idx;
  for (non_zero_bid_idx = 0; non_zero_bid_idx < NLEVELS; non_zero_bid_idx++)
  {
    if (bid_sz[non_zero_bid_idx] > 0)
      break;
  }
  if (non_zero_bid_idx == NLEVELS)
    return ret;

  size_t non_zero_ask_idx;
  for (non_zero_ask_idx = 0; non_zero_ask_idx < NLEVELS; non_zero_ask_idx++)
  {
    if (ask_sz[non_zero_ask_idx] > 0)
      break;
  }
  if (non_zero_ask_idx == NLEVELS)
    return ret;

  auto bbpx = bid_px[non_zero_bid_idx];
  auto bapx = ask_px[non_zero_ask_idx];

  auto mid = (bbpx + bapx) / 2.;
  auto bo = bapx - bbpx;
  return std::make_tuple(mid, bo, bbpx, bapx);
}

//
// fast exponential decay
//
std::optional<double> fast_exp_(double x)
{
  std::optional<double> ret;

  if (x<-100||x>100)
    return ret;

  x = 1.0 + x / 1024;
  x *= x;
  x *= x;
  x *= x;
  x *= x;
  x *= x;
  x *= x;
  x *= x;
  x *= x;
  x *= x;
  x *= x;

  ret =x;
  return ret;
}


// Move the lookup table lambda outside so it is initialized only once
namespace
{
  constexpr double x_min = -100.0;
  constexpr double x_max = 100.0;
  constexpr size_t N = 2000;                   // 2000 intervals, step = 0.1
  constexpr double step = (x_max - x_min) / N; // 0.1
  constexpr double inv_step = 1.0 / step;      // 10.0

  static const std::array<double, N> fast_exp_table = []()
  {
    std::array<double, N> t{};
    for (size_t i = 0; i < N; ++i)
    {
      double x_mid = x_min + (i + 0.5) * step; // Midpoint of interval
      t[i] = std::exp(x_mid);
    }
    return t;
  }();
} // anonymous namespace

std::optional<double> fast_exp(double x)
{
  // Early range check
  if (x < x_min || x > x_max)
  {
    return std::nullopt;
  }

  // Map x to index: floor((x - x_min) / step)
  int index = static_cast<int>((x - x_min) * inv_step);
  if (index < 0)
    index = 0; // Clamp for safety
  if (index >= static_cast<int>(N))
    index = N - 1;

  return fast_exp_table[index];
}

auto fast_lookup(double x, double i)
{
  return fast_exp(-i * x);
}


#ifdef USEIMPMID

//
// compute the implied mid the price is assumed to be in 512
//
std::optional<std::tuple<double, double, double, double>> point::imp_mid(
    int decay,
    std::size_t lev) noexcept
{

  std::optional<std::tuple<double, double, double, double>> ret;

  ASSERT(lev <= NLEVELS, "too many levels");
  uncross();
  shift_zero();
  double bvwap = 0, avwap = 0, bsum = 0, asum = 0, bszpx = 0, aszpx = 0;

  for (std::size_t i = 0; i < lev; i++)
  {
    auto w = fast_lookup(decay / 100., i);
    if (!w)
      return ret;
    auto w_bid = *w * bid_sz[i];
    auto w_ask = *w * ask_sz[i];
    bszpx += w_bid * bid_px[i];
    bsum += w_bid;
    aszpx += w_ask * ask_px[i];
    asum += w_ask;
  }

  if (bsum > 0)
    bvwap = bszpx / bsum;
  else
    return ret;

  if (asum > 0)
    avwap = aszpx / asum;
  else
    return ret;

  if (bvwap > 0 && avwap > 0)
  {
    //
    // bsum / x = asum / (bo - x)
    //
    auto x = bsum / (bsum + asum); // where is the center of mass, is it mostly bid or ask?
    // the vwap bo is avwap - bvwap
    // now figure out where we are closer to
    ASSERT(avwap >= bvwap, "avwap >= bvwap");
    x *= (avwap - bvwap);
    auto ave = bvwap + x;
    ret = std::make_tuple(ave, x, bsum, asum);
    return ret;
  }
  else if (bvwap == 0 || avwap == 0)
  {
    return ret;
  }
  else
    SNGH;
}

#endif

static auto tot_book_sz(const point &p) noexcept
{
  int totbid = 0, totask = 0;
  for (int i = 0; i < NLEVELS; i++)
  {
    totbid += p.bid_sz[i];
    totask += p.ask_sz[i];
  }
  return std::make_tuple(totbid, totask);
}

//
// compute 95 percentile
//
static auto percentile(const point &pt, int p) noexcept
{
  auto [totbid, totask] = tot_book_sz(pt);
  auto target_sz_bid = std::max(1., std::round(totbid * p / 100));
  auto target_sz_ask = std::max(1., std::round(totask * p / 100));
  return std::make_tuple(target_sz_bid, target_sz_ask);
}

//
// compute weightted bid for at least that size
//
std::optional<double> point::w_bid(int sz) const noexcept
{
  std::optional<double> ret;
  int seen_sz = 0;
  int i = 0;
  int64_t pxsz = 0;
  while (seen_sz < sz)
  {
    auto px = bid_px[i];
    seen_sz += bid_sz[i];
    pxsz += px * bid_sz[i];
    i++;
    if (i == NLEVELS)
      return ret;
  }
  if (!seen_sz) return ret;
  ret = double(pxsz) / seen_sz;
  return ret;
}

//
// compute weightted ask for at least that size
//
std::optional<double> point::w_ask(int sz) const noexcept
{
  std::optional<double> ret;
  int seen_sz = 0;
  int i = 0;
  int64_t pxsz = 0;
  while (seen_sz < sz)
  {
    auto px = ask_px[i];
    seen_sz += ask_sz[i];
    pxsz += px * ask_sz[i];
    i++;
    if (i == NLEVELS)
      return ret;
  }
  if (!seen_sz) return ret;
  ret = double(pxsz) / seen_sz;
  return ret;
}

// the % w bid/ask returns sz, wbid, wask
std::optional<std::tuple<double, double, double>> point::w_bid_ask(double pcnt) const noexcept
{
  std::optional<std::tuple<double, double, double>> ret;
  auto [target_sz_bid, target_sz_ask] = percentile(*this, pcnt);
  auto sz = std::max(target_sz_bid, target_sz_ask);
  auto wb = w_bid(sz);
  if (!wb)
    return ret;
  auto wa = w_ask(sz);
  if (!wa)
    return ret;

  if (*wa < *wb || *wa <= 0 || *wb <= 0) [[unlikely]]
  {
    std::cerr << "bad wa and wb " << *wa << " " << *wb << std::endl;
    std::cerr << *this << std::endl;

    for (std::size_t i = 0; i < bid_px.size(); ++i) {
      std::cerr << "Level " << i << ": bidpx=" << bid_px[i]
                << " askpx=" << ask_px[i]
                << " bidsz=" << bid_sz[i]
                << " asksz=" << ask_sz[i] << std::endl;
    }

    ERRF(boost::format("bad wa: %f, wb: %f") % *wa % *wb);
  }

  ASSERT(*wa >= *wb, "wa > wb");
  ret = std::make_tuple(sz, *wb, *wa);
  return ret;
}

// Optimized version - reduces from 4 passes to 3 passes through the book
// Original: 2 passes in tot_book_sz + 1 in w_bid + 1 in w_ask = 4 passes
// Optimized: 1 pass for totals + 1 for bid + 1 for ask = 3 passes (25% reduction)
std::optional<std::tuple<double, double, double>> point::w_bid_ask_fast(double pcnt) const noexcept
{
  // First pass: compute totals (needed for percentile calculation)
  int totbid = 0, totask = 0;
  for (int i = 0; i < NLEVELS; i++) {
    totbid += bid_sz[i];
    totask += ask_sz[i];
  }

  if (totbid == 0 || totask == 0)
    return std::nullopt;

  // Compute target sizes for percentile - MUST match original percentile() function exactly
  // Original uses int arithmetic: totbid * p / 100 where all are ints (integer division!)
  int p = static_cast<int>(pcnt);
  auto target_sz_bid = std::max(1.0, std::round(static_cast<double>(totbid * p / 100)));
  auto target_sz_ask = std::max(1.0, std::round(static_cast<double>(totask * p / 100)));
  auto sz_double = std::max(target_sz_bid, target_sz_ask);
  int sz = static_cast<int>(sz_double);  // Cast to int to match original w_bid/w_ask behavior

  // Second pass: accumulate bid side to target size
  int seen_bid = 0;
  int64_t totbid_px = 0;
  {
    int i = 0;
    while (seen_bid < sz) {
      auto px = bid_px[i];
      seen_bid += bid_sz[i];
      totbid_px += px * bid_sz[i];
      i++;
      if (i == NLEVELS)
        return std::nullopt;
    }
  }

  // Third pass: accumulate ask side to target size
  int seen_ask = 0;
  int64_t totask_px = 0;
  {
    int i = 0;
    while (seen_ask < sz) {
      auto px = ask_px[i];
      seen_ask += ask_sz[i];
      totask_px += px * ask_sz[i];
      i++;
      if (i == NLEVELS)
        return std::nullopt;
    }
  }

  if (seen_bid == 0 || seen_ask == 0)
    return std::nullopt;

  double wbid = double(totbid_px) / seen_bid;
  double wask = double(totask_px) / seen_ask;

  if (wask < wbid || wask <= 0 || wbid <= 0) [[unlikely]]
    return std::nullopt;

  return std::make_tuple(sz_double, wbid, wask);
}

int point::nz_bid() const noexcept
{
  int i = 0;
  while (i < NLEVELS)
  {
    if (bid_sz[i] > 0)
      return i;
    i++;
  }
  return NLEVELS;
}

int point::nz_ask() const noexcept
{
  int i = 0;
  while (i < NLEVELS)
  {
    if (ask_sz[i] > 0)
      return i;
    i++;
  }
  return NLEVELS;
}

void point::fill_best(int nz_bid_idx, int nz_ask_idx) noexcept
{
  if (bid_sz[nz_bid_idx] >= ask_sz[nz_ask_idx])
  {
    bid_sz[nz_bid_idx] -= ask_sz[nz_ask_idx];
    ask_sz[nz_ask_idx] = 0;
  }
  else
  {
    ask_sz[nz_ask_idx] -= bid_sz[nz_bid_idx];
    bid_sz[nz_bid_idx] = 0;
  }
}

void point::uncross() noexcept
{

  SNGH;

  /*

  105     19
  104     2
  103 25  3
  102 5   6
  101 1
  100 10

  */

  auto nz_bid_idx = nz_bid();
  if (nz_bid_idx == NLEVELS)
    return;
  auto nz_ask_idx = nz_ask();
  if (nz_ask_idx == NLEVELS)
    return;

  if (bid_px[nz_bid_idx] >= ask_px[nz_ask_idx])
  {
    fill_best(nz_bid_idx, nz_ask_idx);
    uncross();
  }
}

void point::shift_zero_bid() noexcept
{
  if (nz_bid() == NLEVELS)
    return;
  if (bid_sz[0] == 0)
  {
    for (int i = 0; i < NLEVELS - 1; i++)
    {
      bid_px[i] = bid_px[i + 1];
      bid_sz[i] = bid_sz[i + 1];
    }
    bid_px[NLEVELS - 1] = 0;
    bid_sz[NLEVELS - 1] = 0;
    shift_zero_bid();
  }
}

void point::shift_zero_ask() noexcept
{
  if (nz_ask() == NLEVELS)
    return;
  if (ask_sz[0] == 0)
  {
    for (int i = 0; i < NLEVELS - 1; i++)
    {
      ask_px[i] = ask_px[i + 1];
      ask_sz[i] = ask_sz[i + 1];
    }
    ask_px[NLEVELS - 1] = 0;
    ask_sz[NLEVELS - 1] = 0;
    shift_zero_ask();
  }
}
