#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include <string>
#include <limits>
#include <boost/core/noncopyable.hpp>
#include <boost/format.hpp>


template<typename T>
struct unconstrained
{
  bool operator()([[maybe_unused]] const T& a) const noexcept
  {
    return true;
  }
};

template<typename T>
struct pos_constraint
{
  bool operator()(const T& a) const noexcept
  {
    return a >= 0;
  }
};

template<>
struct pos_constraint<std::string>
{
  bool operator()(const std::string& a) const noexcept
  {
    return a.size() > 0;
  }
};

template<typename T, int mn, int mx>
struct rng_constraint
{
  bool operator()(const T& a) const noexcept
  {
    return a >= mn && a <= mx;
  }
};

template<typename T>
[[maybe_unused]]
static bool check_nan([[maybe_unused]] const T& val) noexcept
{
  return true;
}

template<>
[[maybe_unused]]
 bool check_nan(const double & val) noexcept
{
#ifndef CONSTR_NO_CHECK_NAN
  if (std::isnan(val)) return false;
  if (std::isinf(val)) return false;
#endif
  return val == val;
}

template<>
[[maybe_unused]]
 bool check_nan(const float& val) noexcept
{
  return val == val;
}

template<typename T>
static int convert_to_int(const T&) noexcept
{
  ERR("not convertible to int");
  return 0;
}

template<>
[[maybe_unused]]
 int convert_to_int(const int & val) noexcept
{
  return val;
}

template<typename T>
static double convert_to_double(const T&) noexcept
{
  ERR("not convertible to int");
  return 0;
}

template<>
[[maybe_unused]]
 double convert_to_double(const double& val) noexcept
{
  return val;
}

template<typename T, typename C = unconstrained<T>>
class place //: boost::noncopyable
{
private:
  T value;
  bool _has_value;
  place(T v) = delete;
  place(T&& v) = delete;
  place(const T&& v) = delete;
  place(T& v) {
    value=v.value;
    _has_value=v._has_value;
  }
  C constraint;
public:
  inline void operator=(const place& other) noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    ASSERT( CHTYPEID( T ) == CHTYPEID( other.value ), "incompat types" );
    ASSERT(!_has_value, "already assigned");
    if ((!_has_value) * (!other._has_value)) return ; // *this;
    ASSERT(other._has_value, "copy from unasigned");
#endif
    value = other.value;
    _has_value = true;
    check_constraint();
    //return *this;
  }
  place() noexcept { _has_value = false; }
  inline place(const T& v) noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    ASSERT(!_has_value, "already assigned");
    ASSERT(v._has_value, "copy from unasigned");
#endif
    value = v;
    check_constraint();
  }
  inline bool has_value() const noexcept { return _has_value; }
  inline void set(const T &val) noexcept {
    value=val;
    _has_value=true;
  }
  inline void operator=(const T& val) noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    if CHUNLIKELY( _has_value )
    {
      ERR("value already assigned");
      //return *this;
    }
#endif
    value = val;
    _has_value = true;
    check_constraint();
    //return *this;
  }
  inline void reset_or_init(const T& val) noexcept
  {
    _has_value = true;
    value = val;
    check_constraint();
  }
  inline void reset(const T& val) noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    ASSERT(_has_value, "resetting without value");
#endif
    value = val;
    check_constraint();
  }
  inline void clear() noexcept { _has_value = false; }
  inline T get() const noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    if (!_has_value) ERR("unassigned");
#endif
    return value;
  }
  const T& getr() const noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    if (!_has_value) ERR("unassigned");
#endif
    return value;
  }
  inline operator T() const noexcept
  {
    return get();
  }
private:
  inline void check_constraint() const noexcept
  {
#ifndef PLACES_NO_CHECK_CONSTRAINT
    ASSERT(_has_value, "must have value");
    auto cret = constraint(value);
    ASSERT(cret, boost::str(boost::format("constraint failed value: %d") % value).c_str());
    auto nret = check_nan(value);
    ASSERT(nret, "nan check failed");
#endif
  }
};


typedef place<int> pint;
typedef place<uint64_t> puint64;
typedef place<int, pos_constraint<int>> pos_pint;
typedef place<double> pdouble;
typedef place<double, pos_constraint<double>> pos_pdouble;
typedef place<bool> pbool;
typedef place<std::string> pstring;


