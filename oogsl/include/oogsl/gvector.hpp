#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <gsl/gsl_vector.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_sort_double.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_multifit.h>
#include <boost/format.hpp>
#include <string>
#include <vector>
#include <cmath>
#include <boost/circular_buffer.hpp>
#include "chutil/Assert.hpp"
#include <boost/tuple/tuple.hpp>

// see http://www.mathkeisan.com/UsersGuide/e/man.html#BLAS
// see http://www.csse.uwa.edu.au/programming/gsl-1.0/

namespace oogsl
{
	class gvector
	{

	public:
		gsl_vector *v;
		bool is_view;
		std::size_t n;
		gsl_vector_view temp_view;

		gvector()
		{
			v = 0;
			n = 0;
			is_view = false;
		}

		gvector(std::size_t _n, double x = 0) : n(_n)
		{
			v = gsl_vector_alloc(_n);
			gsl_vector_set_all(v, x);
			is_view = false;
		}

		gvector(std::size_t _n, double min, double max) : n(_n)
		{
			v = gsl_vector_alloc(_n);
			is_view = false;
			double incr = (max - min) / n;
			double x = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				(*this)(i, x);
				x += incr;
			}
		}

		gvector(const boost::circular_buffer<double> &b)
		{
			n = b.size();
			check();
			v = gsl_vector_alloc(n);
			is_view = false;
			for (std::size_t i = 0; i < n; i++)
				(*this)(i, b[i]);
		}

		gvector(const boost::circular_buffer<int> &b)
		{
			n = b.size();
			check();
			v = gsl_vector_alloc(n);
			is_view = false;
			for (std::size_t i = 0; i < n; i++)
				(*this)(i, b[i]);
		}

		gvector(const std::vector<double> &stdv) // this copies
		{
			n = stdv.size();
			check();
			v = gsl_vector_alloc(n);
			is_view = false;
			for (std::size_t i = 0; i < n; i++)
				(*this)(i, stdv[i]);
		}

		gvector(std::vector<double> &stdv) // this happens without copying
		{
			temp_view = gsl_vector_view_array(&stdv[0], stdv.size());
			v = &temp_view.vector;
			n = temp_view.vector.size;
			is_view = true;
		}

		gvector(double *dat, std::size_t _n)
		{
			ASSERT(n > 0, "n=0");
			temp_view = gsl_vector_view_array(dat, _n);
			v = &temp_view.vector;
			n = temp_view.vector.size;
			is_view = true;
		}

		gvector(const double *dat, std::size_t _n)
		{
			ASSERT(n > 0, "n=0");
			v = gsl_vector_alloc(_n);
			for (std::size_t i = 0; i < _n; i++)
			{
				gsl_vector_set(v, i, dat[i]);
			}
			is_view = false;
			n = _n;
		}

		gvector(const std::deque<double> &dq)
		{
			n = dq.size();
			check();
			v = gsl_vector_alloc(n);
			for (std::size_t i = 0; i < n; i++)
			{
				gsl_vector_set(v, i, dq[i]);
			}
			is_view = false;
		}

		gvector(gsl_vector_view &view)
		{
			v = &view.vector;
			n = view.vector.size;
			is_view = true;
		}

		gvector(gsl_vector *src)
		{
			v = src;
			n = src->size;
			is_view = true;
			check();
		}

		gvector(const gsl_vector *src)
		{
			n = src->size;
			check();
			is_view = false;
			v = gsl_vector_alloc(n);
			for (std::size_t i = 0; i < n; i++)
				(*this)(i, gsl_vector_get(src, i));
		}

		/*
		  vector(vector&&swap)
		  {
		  v=swap.v;
		  n=swap.n;
		  swap.v=0;
		  swap.n=0;
		  is_view=false;
		  }
		  */

		gvector(const gvector &src)
		{
			if (src.n > 0)
			{
				v = gsl_vector_alloc(src.n);
				gsl_vector_memcpy(v, src.v);
				is_view = false;
				n = src.n;
				check();
			}
			else
			{
				n = 0;
				v = 0;
			}
		}

		~gvector()
		{
			if (!is_view)
			{
				if (n > 0)
					gsl_vector_free(v);
			}
		}

		operator gsl_vector *()
		{
			return v;
		}

		operator const gsl_vector *() const
		{
			return v;
		}

		inline void check() const
		{
			ASSERT(n > 0, "n=0");
		}

		gvector
		operator=(const gvector &src)
		{
			if (this == &src)
				return *this;
			if (n != src.n)
			{
				ASSERT(!is_view, "cannot assign to a view gvector");
				if (n > 0)
					gsl_vector_free(v);
				v = gsl_vector_alloc(src.n);
				gsl_vector_memcpy(v, src.v);
				is_view = false;
				n = src.n;
			}
			else
			{
				for (std::size_t i = 0; i < n; i++)
					(*this)(i, src(i));
			}
			return *this;
		}

		// get
		double operator()(std::size_t i) const
		{
			return gsl_vector_get(v, i);
		}

		// set
		void operator()(std::size_t i, double x)
		{
			gsl_vector_set(v, i, x);
		}

		void operator+=(const gsl_vector *b)
		{
			gsl_vector_add(v, b);
		}

		void operator+=(const double x)
		{
			gsl_vector_add_constant(v, x);
		}

		void operator-=(const double x)
		{
			gsl_vector_add_constant(v, -x);
		}

		void operator-=(const gsl_vector *b)
		{
			gsl_vector_sub(v, b);
		}

		void operator*=(const gsl_vector *b)
		{
			gsl_vector_mul(v, b);
		}

		void operator/=(const gsl_vector *b)
		{
			gsl_vector_div(v, b);
		}

		void operator/=(const double x)
		{
			gsl_vector_scale(v, 1 / x);
		}

		void operator*=(const double x)
		{
			gsl_vector_scale(v, x);
		}

		double dist(const gsl_vector *b)
		{
			check();
			double _sum = 0;
			int cnt = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				auto d = (*this)(i)-gsl_vector_get(b, i);
				_sum += d * d;
				cnt++;
			}
			_sum /= cnt;
			return ::sqrt(_sum);
		}

		// This routine performs the following vector operation:
		//                                                   n
		//              DDOT  <--  (transpose of x) * y  =  Sum  x(i)*y(i)
		//                                                  i=1
		//      where x and y are double precision vectors.
		//      If n <= 0, DDOT is set to 0.
		double dot(const gsl_vector *Y)
		{
			check();
			double ret;
			int rc = gsl_blas_ddot(v, Y, &ret);
			ASSERT(!rc, "bad return");
			return ret;
		}

		// searches  a double precision vector for the first occurrence of
		// the the maximum absolute value.
		std::size_t iamax()
		{
			check();
			return gsl_blas_idamax(v);
		}

		double asum()
		{
			check();
			return gsl_blas_dasum(v);
		}

		double sum() const
		{
			check();
			double _sum = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				_sum += (*this)(i);
			}
			return _sum;
		}

		gvector cumsum() const
		{
			check();
			gvector ret(n);
			double _sum = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				_sum += (*this)(i);
				ret(i, _sum);
			}
			return ret;
		}

		double sharpe() const
		{
			check();
			auto m = mean();
			auto s = sd(m);
			return m / s * 15.8;
		}

		double wsharpe(const std::vector<double> &w) const
		{
			check();
			auto m = wmean(w);
			auto s = wsd(w);
			return m / s * 15.8;
		}

		double max_dd() const
		{
			check();
			gvector dd(n);
			double lastmax = 0;
			double cumpnl = 0;
			double mdd = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				cumpnl += (*this)(i);
				if (cumpnl > lastmax)
					lastmax = cumpnl;
				if (lastmax - cumpnl > mdd)
					mdd = lastmax - cumpnl;
			}
			if (mdd == 0)
				return mean();
			return mdd;
		}

		/*

		  Extrema value statistics is fastest converging one.
		  I suggest following measuremnts:
		  1. Volatility:
		  a. Range.
		  b. IQR (interquantile range).
		  2. Skewness:
		  a. (High+Low-2xMedian)/(H-L)
		  b. High+Low-2xMedian)/IQR
		  c. (q3+q1-2*median)/iqr
		  3. Kurtosis:
		  a. Range/IQR

		  */

		double iqr()
		{
			check();
			return quantile(.75) - quantile(.25);
		}

		double bimodality()
		{
			check();
			auto q99 = quantile(.99);
			auto q75 = quantile(.75);
			auto q25 = quantile(.25);
			auto q01 = quantile(.01);
			return ((q99 - q75) + (q25 - q01)) / (q75 - q25);
		}

		double skewness()
		{
			check();
			double h = quantile(.95);
			double l = quantile(.05);
			double m = median();
			return (h + l - 2 * m) / (h - l);
		}

		double kurtosis()
		{
			check();
			double h;
			double l;
			minmax(&l, &h);
			return (h - l) / iqr();
		}

		double m2mdd()
		{
			check();
			return mean() / max_dd();
		}

		double sd() const
		{
			check();
			return gsl_stats_sd(v->data, v->stride, v->size);
		}

		double sd(double mean) const
		{
			check();
			return gsl_stats_sd_m(v->data, v->stride, v->size, mean);
		}

		double var() const
		{
			check();
			return gsl_stats_variance(v->data, v->stride, v->size);
		}

		double var(double mean) const
		{
			check();
			return gsl_stats_variance_m(v->data, v->stride, v->size, mean);
		}

		double mean() const
		{
			check();
			return gsl_stats_mean(v->data, v->stride, v->size);
		}

		double wmean(const std::vector<double> &w) const
		{
			check();
			OOOGSL_EXCEPT(w.size() == v->size, "wrong size");
			return gsl_stats_wmean(&w[0], v->stride, v->data, v->stride, v->size);
		}

		double wsd(const std::vector<double> &w) const
		{
			check();
			OOOGSL_EXCEPT(w.size() == v->size, "wrong size");
			return gsl_stats_wsd(&w[0], v->stride, v->data, v->stride, v->size);
		}

		double skew() const
		{
			check();
			return gsl_stats_skew(v->data, v->stride, v->size);
		}

		double skew(double mean, double sd) const
		{
			check();
			return gsl_stats_skew_m_sd(v->data, v->stride, v->size, mean, sd);
		}

		double kurt() const
		{
			check();
			return gsl_stats_kurtosis(v->data, v->stride, v->size);
		}

		double kurt(double mean, double sd) const
		{
			check();
			return gsl_stats_kurtosis_m_sd(v->data, v->stride, v->size, mean, sd);
		}

		void
		minmax(double *min, double *max) const
		{
			check();
			gsl_stats_minmax(min, max, v->data, v->stride, v->size);
		}

		void
		minmaxidx(std::size_t *mini, std::size_t *maxi) const
		{
			check();
			gsl_stats_minmax_index(mini, maxi, v->data, v->stride, v->size);
		}

		void sort()
		{
			check();
			gsl_sort(v->data, v->stride, v->size);
		}

		double where(double x) const
		{
			check();
			int cnt = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				if ((*this)(i) <= x)
					cnt++;
			}
			return double(cnt) / n;
		}

		double mean(int cnt) const
		{
			check();
			double sum = 0;
			for (int i = 0; i < cnt; i++)
			{
				sum += (*this)(i);
			}
			return sum / cnt;
		}

		/*
				double where_last(int h) const
				{
					check();
					ASSERT(h > 0, "bad lookbcak in where_last");
					if (unsigned(h) > n)
						return .5;
					double _sum = 0;
					int cnt = 0;
					for (int i = 0; i < h; i++)
					{
						int idx = n - i - 1;
						if (idx > 0)
						{
							_sum += (*this)(idx);
							cnt++;
						}
					}
					if (cnt == 0)
						return where((*this)(n - 1));
					else
						return where(_sum / cnt);
				}
		*/

		double median()
		{
			check();
			return gsl_stats_median_from_sorted_data(v->data, v->stride, v->size);
		}

		double quantile(double f)
		{
			check();
			return gsl_stats_quantile_from_sorted_data(v->data, v->stride, v->size, f);
		}

		double acorr() const
		{
			check();
			return gsl_stats_lag1_autocorrelation(v->data, v->stride, v->size);
		}

		double acorr(double mean) const
		{
			check();
			return gsl_stats_lag1_autocorrelation_m(v->data, v->stride, v->size, mean);
		}

		static double
		corr(const gvector &v1, const gvector &v2)
		{
			v1.check();
			v2.check();
			ASSERT(v2.v->size == v1.v->size, "bad size");
#ifdef dontuse___
			return gsl_stats_correlation(
				v1.v->data, v1.v->stride,
				v2.v->data, v2.v->stride, v2.v->size);
#else
			gvector v1_copy(v1);
			gvector v2_copy(v2);
			v1_copy /= v1_copy.sd();
			v2_copy /= v2_copy.sd();
			auto ret = gsl_stats_covariance(
				v1_copy.v->data,
				v1_copy.v->stride,
				v2_copy.v->data,
				v2_copy.v->stride,
				v1_copy.v->size);
			return ret;
#endif
		}

		double
		r2() const
		{
			check();
			gvector x(n);
			for (unsigned i = 0; i < n; i++)
				x(i, i);
			auto c = corr(x, *this);
			return c * c;
		}

		static double
		cov(const gvector &v1, const gvector &v2)
		{
			v1.check();
			v2.check();
			ASSERT(v2.v->size == v1.v->size, "bad size");
			return gsl_stats_covariance(
				v1.v->data, v1.v->stride,
				v2.v->data, v2.v->stride, v2.v->size);
		}

		static double
		cov(const gvector &v1, const gvector &v2, double mean1, double mean2)
		{
			v1.check();
			v2.check();
			ASSERT(v2.v->size == v1.v->size, "bad size");
			return gsl_stats_covariance_m(
				v1.v->data, v1.v->stride,
				v2.v->data, v2.v->stride, v2.v->size,
				mean1, mean2);
		}

		// Euclidan norm = ||x||_2 = \sqrt {\sum x_i^2}
		double nrm2() const
		{
			check();
			return gsl_blas_dnrm2(v);
		}

		void
		inverse()
		{
			check();
			for (std::size_t i = 0; i < n; i++)
			{
				double v_ = (*this)(i);
				(*this)(i, 1 / v_);
			}
		}

		void
		sqrt()
		{
			check();
			for (std::size_t i = 0; i < n; i++)
			{
				double v_ = (*this)(i);
				(*this)(i, ::sqrt(v_));
			}
		}

		boost::tuple<double, double, double> // c0,c1,c2 -> Y=c0+c1*X+c2*c2*X
		quadregw(const gvector &w_)
		{
			check();
			double chisq;
			auto X = gsl_matrix_alloc(n, 3);
			auto y = gsl_vector_alloc(n);
			auto w = gsl_vector_alloc(n);
			auto c = gsl_vector_alloc(3);
			auto cov = gsl_matrix_alloc(3, 3);
			for (unsigned i = 0; i < n; i++)
			{
				gsl_matrix_set(X, i, 0, 1.0);
				gsl_matrix_set(X, i, 1, i);
				gsl_matrix_set(X, i, 2, i * i);

				gsl_vector_set(y, i, (*this)(i));
				gsl_vector_set(w, i, w_(i));
			}

			{
				gsl_multifit_linear_workspace *work = gsl_multifit_linear_alloc(n, 3);
				gsl_multifit_wlinear(X, w, y, c, cov,
									 &chisq, work);
				gsl_multifit_linear_free(work);
			}

			gsl_matrix_free(X);
			gsl_vector_free(y);
			gsl_vector_free(w);
			gsl_vector_free(c);
			gsl_matrix_free(cov);

			return boost::tuple<double, double, double>(
				gsl_vector_get(c, 0),
				gsl_vector_get(c, 1),
				gsl_vector_get(c, 2));

			// #define C(i) (gsl_vector_get(c,(i)))
			// #define COV(i,j) (gsl_matrix_get(cov,(i),(j)))

			//     {
			//         printf ("# best fit: Y = %g + %g X + %g X^2\n",
			//                 C(0), C(1), C(2));

			//         printf ("# covariance matrix:\n");
			//         printf ("[ %+.5e, %+.5e, %+.5e  \n",
			//                 COV(0,0), COV(0,1), COV(0,2));
			//         printf ("  %+.5e, %+.5e, %+.5e  \n",
			//                 COV(1,0), COV(1,1), COV(1,2));
			//         printf ("  %+.5e, %+.5e, %+.5e ]\n",
			//                 COV(2,0), COV(2,1), COV(2,2));
			//         printf ("# chisq = %g\n", chisq);
			//     }
		}

		double
		rise() const
		{
			auto lr = linreg();
			auto c1 = boost::get<1>(lr);
			return c1 * n;
		}

		boost::tuple<double, double> // c0,c1 (slope)
		linregw(const gsl_vector *w)
		{
			check();
			double c0, c1, cov00, cov01, cov11, sumsq;
			gvector x(n);
			for (unsigned i = 0; i < n; i++)
				x(i, i);
			gsl_fit_wlinear(x.v->data, 1,
							w->data, 1,
							v->data, 1,
							n,
							&c0, &c1, &cov00, &cov01, &cov11, &sumsq);
			return boost::tuple<double, double>(c0, c1);
		}

		boost::tuple<double, double, double, double, double> // c0,c1 (slope),sd,y(n),yf_err,
		linreg(bool withError = false) const
		{
			check();
			double c0, c1, cov00, cov01, cov11, sumsq;
			gvector x(n);
			for (unsigned i = 0; i < n; i++)
				x(i, i);
			gsl_fit_linear(x.v->data, 1, v->data, 1, n,
						   &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
			double yf = 0., yf_err = 0.;
			if (withError)
			{
				double xf = double(n - 1);
				gsl_fit_linear_est(xf,
								   c0, c1,
								   cov00, cov01, cov11,
								   &yf, &yf_err);
			}
			return boost::tuple<double, double, double, double, double>(c0, c1, ::sqrt(sumsq / n), yf, yf_err);
		}

		boost::tuple<double, double>
		linreg_x_y(const std::vector<double> &x)
		{
			check();
			double c0, c1, cov00, cov01, cov11, sumsq;
			gvector x_(n);
			for (unsigned i = 0; i < n; i++)
				x_(i, x[i]);
			gsl_fit_linear(x_.v->data, 1, v->data, 1, n,
						   &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
			//        double yf = 0., yf_err = 0.;
			return boost::tuple<double, double>(c0, c1);
		}

		boost::tuple<double, double>
		linreg_x_y(const std::vector<int> &x)
		{
			check();
			double c0, c1, cov00, cov01, cov11, sumsq;
			gvector x_(n);
			for (unsigned i = 0; i < n; i++)
				x_(i, x[i]);
			gsl_fit_linear(x_.v->data, 1, v->data, 1, n,
						   &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
			//        double yf = 0., yf_err = 0.;
			return boost::tuple<double, double>(c0, c1);
		}

		boost::tuple<double, double> // c0,c1 (slope)
		wlinear() const
		{
			check();
			double c0, c1, cov00, cov01, cov11, sumsq;
			gvector x(n);
			gvector w(n);
			for (unsigned i = 0; i < n; i++)
			{
				x(i, i);
				w(i, double(i + 1) / n);
			}
			gsl_fit_wlinear(x.v->data, 1,
							w.v->data, 1,
							v->data, 1,
							n,
							&c0, &c1, &cov00, &cov01, &cov11, &sumsq);
			return boost::tuple<double, double>(c0, c1);
		}

		template <class T>
		static std::vector<double> reshape_by_means(const T &in, std::size_t n)
		{
			ASSERT(in.size() > 0, "n=0");
			std::vector<double> ret;
			if (in.size() <= n)
			{
				for (std::size_t i = 0; i < in.size(); ret.push_back(in[i++]))
					;
				return ret;
			}
			ASSERT(in.size() > n, "in too small");
			auto num_per_bucket = in.size() / n;
			std::size_t i = 0;
			while (i < in.size())
			{
				std::vector<double> tmp;
				for (std::size_t k = 0; k < num_per_bucket; k++)
				{
					if (i >= in.size() - 1)
						goto done;
					tmp.push_back(in[i++]);
				}
				double _sum = 0;
				for (std::size_t l = 0; l < tmp.size(); _sum += tmp[l++])
					;
				auto mean = _sum / tmp.size();
				ret.push_back(mean);
			}
		done:
			return ret;
		}

		// the last value in vector is most recent data
		// i.e. data is push_backed into the vector
		std::tuple<unsigned, int>
		runs(const std::vector<double> &w = std::vector<double>()) const
		{
			double m;
			if (w.size() == 0)
				m = mean();
			else
				m = wmean(w);
			std::vector<int> flags;
			for (std::size_t i = 0; i < n; i++)
			{
				auto x = gsl_vector_get(v, i);
				if (x > m)
					flags.push_back(1);
				else
					flags.push_back(-1);
			}
			auto first_flag = flags[0];				  // oldest
			auto last_flag = flags[flags.size() - 1]; // most recent
			int direction;
			if (first_flag == last_flag)
				direction = 0;
			else if (first_flag < last_flag)
				direction = 1;
			else
				direction = -1;
			int prev = 0;
			unsigned nruns = 0;
			for (auto n_ : flags)
			{
				if (n_ == prev)
					continue;
				nruns++;
				prev = n_;
			}
			return std::make_tuple(nruns, direction);
		}

		std::string to_string() const
		{
			std::string ret = "";
			for (std::size_t i = 0; i < n; i++)
			{
				ret += (boost::format("%1%") % gsl_vector_get(v, i)).str();
				ret += " ";
			}
			return ret;
		}

		friend std::ostream &operator<<(std::ostream &out, const gvector &v)
		{
			return out << v.to_string();
		}
	};

}
