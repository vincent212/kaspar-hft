
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/uniform_dist.hpp"

using namespace std;
using namespace chutil;

uniform_random::uniform_random()
  :generator(104395301), // prime number seed
  uni_dist(0, 1),
  uni(generator, uni_dist)
{}

double
uniform_random::uniform() {
  return uni();
}

double uniform_random::uniform(double lb, double ub)
{
  auto r = uni() * (ub - lb) + lb;
  return r;
}

bool
uniform_random::unfair_coin(double fairness, int times_to_flip) {
  for (int i = 0; i < times_to_flip; i++) {
    auto u = uni();
    if (u < fairness)
      return true;
  }
  return false;
}


