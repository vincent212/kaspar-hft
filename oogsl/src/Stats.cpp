
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <boost/assign/list_of.hpp>
#include <boost/assign/list_inserter.hpp>

#include <vector>

#include "oogsl/Stats.hpp"
#include "oogsl/gvector.hpp"

#include "chutil/Assert.hpp"

using namespace std;
using namespace oogsl;
using namespace boost::bimaps;

Stats::Stats(std::size_t size):sz(size) 
{
}

void
Stats::set_size(std::size_t size)
{
    auto &l=listset.left;
    if (l.size()>size)
    {
        auto p=l.begin();
        std::size_t cnt=0;
        while (cnt<size)
        {
            p=l.erase(p);
            if (p==l.end())
                break;
            cnt++;
        }
    }
    sz=size;
}

// insert at end and delete at front
void
Stats::push_back(double x)
{
    boost::assign::push_back(listset.left) (x,x);
    auto &l=listset.left;
    if (l.size()>sz)
    {
        l.erase(l.begin());
    }
}

vector<double>
Stats::get_in_order() const
{
    vector<double> ret;
    for (const auto&p : listset.left)
    {
        ret.push_back(p.first);
    }
    return ret;
}

double
Stats::min_() const
{
    if (listset.right.size()>0)
    {
        return listset.right.begin()->second;
    }
    else
        throw "no data yet";
}

double
Stats::max_() const
{
    if (listset.right.size()>0)
    {
        return listset.right.rbegin()->second;
    }
    else
        throw "no data yet";
}

vector<double>
Stats::get_sorted() const
{
    vector<double> ret;
    for (const auto &p: listset.right)
    {
        ret.push_back(p.first);
    }
    return ret;
}

double
Stats::where(double x) const
{
    int cnt=0;
    for (const auto &p : listset.left)
    {
        if (p.first <= x)
            cnt++;
    }
    return double(cnt)/listset.size();
}

double
Stats::mean() const
{
    int cnt=0;
    double sum=0;
    for (const auto&p : listset.left)
    {
        sum+=p.first;
        cnt++;
    }
    return sum/cnt;
}

std::tuple<double,double> 
Stats::linreg() const
{
    std::tuple<double,double> ret;
    const auto&v=get_in_order();
    if (v.size()==0)
        throw "no items here";
    oogsl::gvector gv(v);
    auto lr=gv.linreg();
    get<0>(ret)=get<1>(lr);
    get<1>(ret)=get<0>(lr);
    return ret;
}

std::tuple<double,double> 
Stats::linregw() const
{
    std::tuple<double,double> ret;
    const auto&v=get_in_order();
    if (v.size()==0)
        throw "no items here";
    oogsl::gvector gv(v);

    // generate linearly decaying weights vector
    // the first weight is 1/n
    
    auto incr=1/v.size();
    oogsl::gvector w(gv.n);
    for (std::size_t i=0;i<w.n;i++)
        w(i,double(incr*(i+1)));

    auto lr=gv.linregw(w);
    get<0>(ret)=get<1>(lr);
    get<1>(ret)=get<0>(lr);
    return ret;
}

std::tuple<double,double,double>
Stats::quadreg() const
{
    std::tuple<double,double,double> ret;
    const auto&v=get_in_order();
    if (v.size()==0)
        throw "no items here";
    oogsl::gvector gv(v);

    oogsl::gvector w(gv.n);
    for (std::size_t i=0;i<w.n;i++)
        w(i,1);

    auto lr=gv.quadregw(w);
    get<0>(ret)=get<2>(lr);
    get<1>(ret)=get<1>(lr);
    get<1>(ret)=get<0>(lr);
    return ret;    
}

std::tuple<double,double,double>
Stats::quadregw() const
{
    std::tuple<double,double,double> ret;
    const auto&v=get_in_order();
    if (v.size()==0)
        throw "no items here";
    oogsl::gvector gv(v);

    // generate linearly decaying weights vector
    // the first weight is 1/n
    
    auto incr=1/v.size();
    oogsl::gvector w(gv.n);
    for (std::size_t i=0;i<w.n;i++)
        w(i,double(incr*(i+1)));

    auto lr=gv.quadregw(w);
    get<0>(ret)=get<2>(lr);
    get<1>(ret)=get<1>(lr);
    get<1>(ret)=get<0>(lr);
    return ret;
}

double
Stats::r2() const
{
    const auto&v=get_in_order();
    if (v.size()==0)
        throw "no items here";
    oogsl::gvector gv(v);
    return gv.r2();
}

double
Stats::percentile(double x) const
{
    auto &s=listset.right;
    if (s.size()==1)
    {
        return s.begin()->first;
    }
    else if (s.size()==0)
    {
        throw "no elements yet";
    }
    assert(s.size()<=sz);
    auto idx=(s.size()-1)*x;
    auto lidx=int(idx);
    auto uidx=lidx+1;
    auto p=s.begin();
    for (int i=0;i<lidx;i++)
    {
        ++p;
    }
    double lidx_val=p->first;
    ++p;
    double uidx_val=p->first;
    auto d_lidx=double(lidx)/(s.size()-1);
    auto d_uidx=double(uidx)/(s.size()-1);
    assert(d_lidx<=x && d_uidx>=x);
    auto r=(x-d_lidx)/(d_uidx-d_lidx);
    assert(r>=0);
    return lidx_val+r*(uidx_val-lidx_val);
}

bool
Stats::can_calc() const
{
    auto &s=listset.right;
    if (s.size()>2)
        return true;
    else
        return false;
}

bool
Stats::is_full() const
{
    if (sz==0) 
        return false;
    auto &s=listset.right;
    return s.size()==sz;
}

