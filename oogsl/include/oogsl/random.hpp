#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include "permutation.hpp"

namespace oogsl
{

  class random
  {
    static int initialized;

    static void
      init()
    {
      /*This function reads the environment variables GSL_RNG_TYPE and GSL_RNG_SEED and uses their values to set the corresponding library variables gsl_rng_default and gsl_rng_default_seed. These global variables are defined as follows,

              extern const gsl_rng_type *gsl_rng_default
              extern unsigned long int gsl_rng_default_seed

              The environment variable GSL_RNG_TYPE should be the name of a generator, such as taus or mt19937. The environment variable GSL_RNG_SEED should contain the desired seed value. It is converted to an unsigned long int using the C library function strtoul.

              If you don't specify a generator for GSL_RNG_TYPE then gsl_rng_mt19937 is used as the default. The initial value of gsl_rng_default_seed is zero.
      */
      gsl_rng_env_setup();
    }

    const gsl_rng_type* T;
    gsl_rng* r;

  public:

    random()
    {
      if (!initialized)
      {
        init();
        initialized = 1;
      }
      T = gsl_rng_default;
      r = gsl_rng_alloc(T);
    }

    operator gsl_rng* ()
    {
      return r;
    }

    double uniform()
    {
      return gsl_rng_uniform(r);
    }

    bool unfair_coin(double thresh, int count = 1)
    {
      bool hit = false;
      for (int i = 0; i < count; i++)
      {
        bool trial = uniform() < thresh;
        hit |= trial;
      }
      return hit;
    }

    void shuffle(gsl_permutation* p)
    {
      gsl_ran_shuffle(r, p->data, p->size, sizeof(std::size_t));
    }

    ~random()
    {
      gsl_rng_free(r);
    }

  };

  int random::initialized = 0;

}
