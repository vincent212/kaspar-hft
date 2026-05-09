/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <gsl/gsl_vector.h>
//#include <gsl/gsl_blas.h>
//#include <gsl/gsl_statistics.h>
//#include <gsl/gsl_sort_double.h>
#include <gsl/gsl_fit.h>

int func(const std::vector<double>&stdv)
{

    auto temp_view=gsl_vector_view_array(&stdv[0],stdv.size());
    auto v=&temp_view.vector;
    auto n=temp_view.vector.size;
    double c0,c1,cov00,cov01,cov11,sumsq;

    auto x=gsl_vector_alloc(n);
    for (uint i=0;i<n;i++)
        gsl_vector_set(x,i,i);
    gsl_fit_linear(x.v->data,1,v->data,1,n,
                   &c0,&c1,&cov00,&cov01,&cov11,&sumsq);

    if (c0<0)
        return -1;
    else
        return 1;
}
