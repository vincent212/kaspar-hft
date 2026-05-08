#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <vector>
#include "chutil/ut.hpp"
#include <iostream>

namespace chutil
{

  struct CSVFileReader
  {

    typedef std::vector<std::vector<std::string> > read_file_ret_t;
    typedef std::vector<std::string> tokenize_ret_t;

	static std::vector<std::string>
		read_lines(const std::string &_fname, std::size_t max_num_lines=0);

    static read_file_ret_t
      readFile2(const std::string &_fname, const std::string &_sep, std::size_t max_num_lines=0)
      {
        read_file_ret_t ret,*tmp;
        tmp=readFile(_fname,_sep,max_num_lines);
        ret=*tmp;
        delete tmp;
        return ret;
      }

    static read_file_ret_t*
      readFile(const std::string &_fname, const std::string &_sep, std::size_t max_num_lines=0);

    static read_file_ret_t*
      readFile(std::ifstream &file, const std::string& _sep, std::size_t max_num_lines = 0);

    static tokenize_ret_t
      tokenize(std::string _str, std::string _sep);

    static std::vector<std::vector<double>>
      to_double(const read_file_ret_t&arr)
    {
      std::vector<std::vector<double>> val;
      for (std::size_t i=0;i<arr.size();i++) {
        val.push_back(std::vector<double>());
        for (std::size_t j=0;j<arr[i].size();j++) {
          val[i].push_back(chutil::to_float(arr[i][j].c_str()));
        }
      }
      return val;
    }

    static std::vector<double>
      to_double(const tokenize_ret_t&line) {
        std::vector<double> ret;
        for (std::size_t i=0;i<line.size();i++)
          ret.push_back(std::stod(line[i].c_str()));
        return ret;
    }

    static std::vector<std::vector<double>>
      transpose(const std::vector<std::vector<double>>&val)
    {
      std::vector<std::vector<double>> ret;
      for (std::size_t i=0;i<val[0].size();i++) {
        ret.push_back(std::vector<double>());
        for (std::size_t j=0;j<val.size();j++) {
          ret[i].push_back(val[j][i]);
        }
      }
      return ret;
    }


  };

}
