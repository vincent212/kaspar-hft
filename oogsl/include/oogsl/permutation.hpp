#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <gsl/gsl_permutation.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_rng.h>

#include <string>
#include <boost/format.hpp>

namespace oogsl
{

	class permutation
	{
		gsl_permutation*p;
		std::size_t n;

	public:

		permutation(std::size_t _n):n(_n)
		{
			p=gsl_permutation_alloc(n);
			gsl_permutation_init(p);
		}

		~permutation()
		{
			gsl_permutation_free(p);
		}

		operator gsl_permutation*()
		{
			return p;
		}

		std::size_t operator()(const std::size_t i)
		{
			return gsl_permutation_get(p,i);
		}

		std::string to_string()
		{
			std::string ret="[";
			for (unsigned i=0;i<n;i++)
			{
				ret+=(boost::format("%1%")%gsl_permutation_get(p,i)).str();
				ret+=" ";
			}
			ret+="]";
			return ret;
		}

	};

}
