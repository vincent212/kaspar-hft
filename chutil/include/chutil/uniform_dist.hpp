#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <ctime>            // std::time

#include <boost/random/uniform_real.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random/mersenne_twister.hpp>

namespace chutil 
{

  struct uniform_random {

    uniform_random();

    typedef boost::mt19937 base_generator_type;
    base_generator_type generator;
    boost::uniform_real<> uni_dist;
    boost::variate_generator<base_generator_type&, boost::uniform_real<> > uni;

    double uniform();
    double uniform(double lb,double ub);
    bool unfair_coin(double fairness,int times_to_flip=1);

  };

}