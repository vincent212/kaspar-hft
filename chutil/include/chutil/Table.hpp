#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <vector>
#include <string>
#include <sstream>
#include <boost/format.hpp>
#include <tuple>


namespace chutil
{

  class Table
  {
    std::vector<std::string> col_labels;
    std::vector<std::vector<std::string> > values;
    std::vector<std::tuple<std::string, std::string>> col_lables_with_ttips;

    uint nrows, row_cntr;
    std::string title;
    uint col_cntr;
    std::size_t num_cols;

  public:

    Table(const std::string& _title, const std::vector<std::string>& labels)
      :nrows(0), title(_title), col_labels(labels), col_cntr(0), row_cntr(0)
    {
    }

    Table(const std::string& _title, const std::vector<std::tuple<std::string, std::string>>& labels)
      :nrows(0), title(_title), col_lables_with_ttips(labels), col_cntr(0), row_cntr(0)
    {
    }

    void add_row()
    {
      ++nrows;
      ++row_cntr;
      col_cntr = 0;
      auto sz = std::max(col_labels.size(), col_lables_with_ttips.size());
      ASSERT(sz > 0, "no columns");
      num_cols = sz;
      values.push_back(std::vector<std::string>(sz, ""));
    }

    void set_row(uint r)
    {
      row_cntr = r;
    }

    void incr_row()
    {
      row_cntr++;
    }

    void set_col(uint c)
    {
      col_cntr = c;
    }

    std::vector<std::string> get_row() const
    {
      return values[row_cntr - 1];
    }

    bool row_is_full() const
    {
      return !(col_cntr < num_cols);
    }

    void set_value(const std::string& s)
    {
      ASSERTF(col_cntr < num_cols, boost::format("bad col index: %d %d %s") % col_cntr % num_cols % s);
      ASSERTF(row_cntr - 1 < values.size(), boost::format("bad row index: %d %d %s") % (row_cntr - 1) % values.size() % s);
      values[row_cntr - 1][col_cntr++] = s;
    }

    void set_value(const char* s)
    {
      boost::format fmt("%s");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(bool b)
    {
      boost::format fmt("%s");
      if (b)
        fmt % "true";
      else
        fmt % "false";
      set_value(fmt.str());
    }

    void set_value(int s)
    {
      boost::format fmt("%d");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(size_t s)
    {
      boost::format fmt("%d");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(uint s)
    {
      boost::format fmt("%d");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(long long s)
    {
      boost::format fmt("%d");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(unsigned long long s)
    {
      boost::format fmt("%d");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(double s)
    {
      boost::format fmt("%f");
      fmt% s;
      set_value(fmt.str());
    }

    void set_value(long double s)
    {
      boost::format fmt("%f");
      fmt% s;
      set_value(fmt.str());
    }

    std::string to_string()
    {
      std::ostringstream ss;
      ss << *this << std::endl;
      return ss.str();
    }

    inline friend std::ostream& operator<<(std::ostream& out, const Table& t)
    {
      out << t.title + "\t";
      if (t.col_labels.size() > 0)
      {
        for (uint i = 0; i < t.col_labels.size(); i++)
          out << t.col_labels[i] << '\t';
      }
      else if (t.col_lables_with_ttips.size() > 0)
      {
        for (uint i = 0; i < t.col_lables_with_ttips.size(); i++)
          out << std::get<0>(t.col_lables_with_ttips[i])
          << ":"
          << std::get<1>(t.col_lables_with_ttips[i])
          << '\t';
      }
      out << std::endl;
      for (uint i = 0; i < t.nrows; i++)
      {
        out << '\t';
        for (uint j = 0; j < t.num_cols; j++)
        {
          out << t.values[i][j] << '\t';
        }
        out << std::endl;
      }
      return out;
    }
  };
}
