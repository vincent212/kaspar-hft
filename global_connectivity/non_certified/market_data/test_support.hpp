// Helpers shared by the decode tests under market_data/<venue>/tests/. Each test
// includes it as "../../test_support.hpp", so it needs no include path of its own.
//
// CHECK is always on: it does not depend on NDEBUG, so the tests check the same
// things in every build mode.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <string_view>
#include <variant>
#include <vector>

namespace test_support
{
inline int checks = 0;

inline void check(bool ok, const char *expr, const char *file, unsigned line)
{
  ++checks;
  if (!ok)
  {
    std::cerr << file << ":" << line << ": CHECK failed: " << expr << "\n";
    std::abort();
  }
}

inline void check(bool ok, const char *expr, const std::source_location &at)
{
  check(ok, expr, at.file_name(), unsigned(at.line()));
}

// Variadic so that a condition with unparenthesized commas (braced lists,
// template arguments) is one argument.
#define CHECK(...) ::test_support::check(bool(__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)

using bytes = std::vector<unsigned char>;

inline const char *cp(const bytes &b) { return reinterpret_cast<const char *>(b.data()); }

// "01 00 4f" or "01004f" -> {0x01, 0x00, 0x4f}. Whitespace is skipped.
inline bytes hex(std::string_view s, const std::source_location &at = std::source_location::current())
{
  auto nibble = [&](char c) -> unsigned {
    if (c >= '0' && c <= '9')
      return unsigned(c - '0');
    if (c >= 'a' && c <= 'f')
      return unsigned(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
      return unsigned(c - 'A' + 10);
    check(false, "hex digit", at);
    return 0;
  };
  bytes out;
  int half = -1;
  for (char c : s)
  {
    if (c == ' ' || c == '\n' || c == '\t')
      continue;
    if (half < 0)
      half = int(nibble(c));
    else
    {
      out.push_back((unsigned char)((unsigned(half) << 4) | nibble(c)));
      half = -1;
    }
  }
  check(half < 0, "even number of hex digits", at);
  return out;
}

// Builds a message in a byte buffer, integers in byte order E. u, c and s
// append; put and str write at an offset into a buffer already sized.
template <std::endian E>
struct writer
{
  bytes b;

  writer() = default;
  explicit writer(size_t n) : b(n, 0) {}

  // The low n bytes of v.
  writer &u(uint64_t v, int n)
  {
    for (int i = 0; i < n; ++i)
      b.push_back((unsigned char)(v >> (8 * (E == std::endian::little ? i : n - 1 - i))));
    return *this;
  }
  writer &c(char v)
  {
    b.push_back((unsigned char)v);
    return *this;
  }
  // v left-aligned in n bytes, padded with pad (cut if longer).
  writer &s(std::string_view v, size_t n, char pad = ' ')
  {
    for (size_t i = 0; i < n; ++i)
      b.push_back((unsigned char)(i < v.size() ? v[i] : pad));
    return *this;
  }
  // v as sizeof(T) bytes at off.
  template <typename T>
  writer &put(size_t off, T v, const std::source_location &at = std::source_location::current())
  {
    check(off + sizeof(T) <= b.size(), "put within the buffer", at);
    const auto w = uint64_t(v);
    for (size_t i = 0; i < sizeof(T); ++i)
      b[off + i] = (unsigned char)(w >> (8 * (E == std::endian::little ? i : sizeof(T) - 1 - i)));
    return *this;
  }
  // v left-aligned in n bytes at off, padded with pad.
  writer &str(size_t off, std::string_view v, size_t n, char pad = ' ',
              const std::source_location &at = std::source_location::current())
  {
    check(off + n <= b.size() && v.size() <= n, "str within the buffer and the field", at);
    for (size_t i = 0; i < n; ++i)
      b[off + i] = (unsigned char)(i < v.size() ? v[i] : pad);
    return *this;
  }

  const char *data() const { return cp(b); }
  size_t size() const { return b.size(); }
};

using le_writer = writer<std::endian::little>;
using be_writer = writer<std::endian::big>;

// The message of a decode result, checked to be a successful decode of an M,
// returned by value (the result may be a temporary).
template <typename M, typename D>
M msg_as(const D &d, const std::source_location &at = std::source_location::current())
{
  check(bool(d), "decode succeeded", at);
  check(std::holds_alternative<M>(d.msg), "decoded message type", at);
  return std::get<M>(d.msg);
}

// Decodes [p, p + n) with decode, checks that it gives an M of `size` bytes,
// and returns the M by value.
template <typename M, typename F>
M decode_as(F decode, const char *p, size_t n, size_t size,
            const std::source_location &at = std::source_location::current())
{
  const auto d = decode(p, n);
  check(bool(d), "decode succeeded", at);
  check(d.size == size, "decoded size", at);
  return msg_as<M>(d, at);
}
} // namespace test_support
