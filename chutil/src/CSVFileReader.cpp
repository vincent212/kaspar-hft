
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include <fstream>
#include <stdio.h>

#include <boost/tokenizer.hpp>
#include "chutil/CSVFileReader.hpp"

#include "boost/algorithm/string.hpp"

using namespace chutil;
using namespace std;

vector<string>
CSVFileReader::read_lines(const string &_fname, std::size_t max_num_lines)
{
	vector<string> ret;

	string line;
	ifstream myfile(_fname.c_str());

	std::size_t cnt = 0;

	if (myfile.is_open())
	{
		while (!myfile.eof())
		{
			cnt++;
			getline(myfile, line);
			if (line == "" || line.size() == 0)
				continue;
			if (line.size() <= 1)
				continue;
			ret.push_back(line);
			if (max_num_lines>0 && cnt >= max_num_lines)
				break;
		}
		myfile.close();
	}
    else
    {
        auto err = string("could not open file:") + _fname;
        cout << err << endl;
        ERR(err.c_str());
    }

	return ret;
}

[[nodiscard]]
CSVFileReader::read_file_ret_t *
CSVFileReader::readFile(const string &_fname, const string &_sep, std::size_t max_num_lines)
{
    ifstream myfile(_fname.c_str());
    CSVFileReader::read_file_ret_t*ret=new CSVFileReader::read_file_ret_t();

    string line;
    std::size_t cnt=0;

    if (myfile.is_open())
    {
        while (! myfile.eof() )
        {
            cnt++;
            getline (myfile,line);
            if (line == "" || line.size() == 0)
                continue;
            if (line.size() <= 1)
                continue;
            if (line[0] == '#')
                continue;
            boost::algorithm::trim(line);
            ret->push_back(tokenize( line, _sep));
            if (max_num_lines>0 && cnt>=max_num_lines)
              break;
        }
        myfile.close();
    }
    else
    {
        auto err = string("could not read file: ") + _fname;
        cout << err << endl;
        cerr << err << endl;
        ERR(err.c_str());
    }

    return ret;
}


CSVFileReader::tokenize_ret_t
CSVFileReader::tokenize(string _str, string _sep)
{
    CSVFileReader::tokenize_ret_t ret;
    ret.clear();

    if (_str.size() == 0)
    {
        return ret;
    }
    typedef boost::tokenizer<boost::char_separator<char> > 
        tokenizer;
    boost::char_separator<char> sep(_sep.c_str(), "", boost::keep_empty_tokens);
    tokenizer tokens(_str, sep);
    for (tokenizer::iterator tok_iter = tokens.begin();
         tok_iter != tokens.end(); ++tok_iter)
        ret.push_back(*tok_iter);

    return ret;
}

