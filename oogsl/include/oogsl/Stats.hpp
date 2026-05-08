#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Assert.hpp"

#include <boost/bimap/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/multiset_of.hpp>

#include <vector>

namespace oogsl
{

	struct Stats
	{
		typedef boost::bimaps::bimap <

			boost::bimaps::list_of< double >,
			boost::bimaps::multiset_of < double >

		> sorted_list_t;

		typedef sorted_list_t::value_type list_val_t;

		std::size_t sz;
		sorted_list_t listset;
		Stats(std::size_t size);
		Stats() { sz = 0; };
		void set_size(std::size_t size);
		void push_back(double);
		std::size_t size() const { return listset.size(); }

		std::vector<double> get_in_order() const;
		std::vector<double> get_sorted() const;

		double percentile(double x) const;
		bool is_full() const;
		bool can_calc() const;
		double where(double x) const;
		double mean() const;
		std::tuple<double, double> linreg() const;
		std::tuple<double, double> linregw() const;
		std::tuple<double, double, double> quadreg() const;
		std::tuple<double, double, double> quadregw() const;
		double r2() const;
		double min_() const;
		double max_() const;

		void clear() { listset.clear(); }


		// more functions
		std::size_t iamax() const;
		double sum() const;
		std::vector<double> cumsum() const;
		double sharpe() const;
		double maxdd() const;
		double sd() const;
		double iqr() const;
		double skewness() const;
		double kurtosis() const;
		double skew() const;
		double kurt() const;
		double acorr() const;

		// even more functions
		unsigned runs() const;
		double rsi(double N) const;

	};

}
