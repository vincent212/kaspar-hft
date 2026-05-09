#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <stdlib.h>
#include <string>
#include <vector>
#include <set>
#include <boost/filesystem.hpp>


namespace chutil
{

  class FileSystem
  {

    boost::filesystem::path wd;

    bool
    is_relative(const std::string&path) const 
    {
      return path.c_str()[0]!='/';
    }

    boost::filesystem::path
    adjust_path(const boost::filesystem::path&p) const
    {
      boost::filesystem::path ret;
      if (!p.has_root_path())
        ret=wd/p;
      else
        ret=p;
      return ret;
    }

  public:

    const boost::filesystem::path&pwd() const
    {
      return wd;
    }

    void
    up()
    {
      wd=wd.parent_path();
    }

    FileSystem(const boost::filesystem::path&_wd): wd(_wd) {}
    FileSystem() : wd("/") {}

    boost::filesystem::path
      get_cwd() {
        auto cwd = boost::filesystem::current_path();
        return cwd;
    }

    static FileSystem*
    home() 
    {
      auto h=getenv("HOME");
      ASSERT(h,"home not defined");
      return new FileSystem(h);
    }

    void cd_home()
    {
      auto h=getenv("HOME");
      ASSERT(h,"home not defined");
      cd(h);
    }

    void
    cd(const boost::filesystem::path&_wd) 
    {
      wd=adjust_path(_wd);
    }

    enum {NO_EXIST,REGULAR,DIRECTORY};

    int
    status(const boost::filesystem::path&file_name) const 
    {
      auto p=adjust_path(file_name);
      if (!boost::filesystem::exists(p))
        return NO_EXIST;
      else if (boost::filesystem::is_regular_file(p))
        return REGULAR;
      else if (boost::filesystem::is_directory(p))
        return DIRECTORY;
      else
        ERR("could not determine file type");
      return -1;
    }

    void
    mkdir(const boost::filesystem::path&dir_name)
    {
      auto p=adjust_path(dir_name);
      if (status(p)==NO_EXIST)
        boost::filesystem::create_directory(p);
    }

    uintmax_t
    size(const boost::filesystem::path&file_name) const 
    {
      auto p=adjust_path(file_name);
      ASSERT(status(file_name)==REGULAR,"not a regular file");
      return boost::filesystem::file_size(p);
    }

    // list files extention includes dot like this: ".bin"
    std::set<boost::filesystem::path>
    lsf(const boost::filesystem::path&dir,const std::string&extension) const 
    {
      std::set<boost::filesystem::path> ret;
      auto p=adjust_path(dir);
      ASSERT(status(p)==DIRECTORY,"not a directory");
      for (auto it=boost::filesystem::directory_iterator(p);
           it!=boost::filesystem::directory_iterator();
           ++it) {
        boost::system::error_code ec;
        if (it->status(ec).type()==boost::filesystem::file_type::regular_file) 
        {
          //cout << it->path().stem() << " " << it->path().extension() << endl;
          if (extension==".*")
            ret.insert(it->path());
          else if (it->path().extension()==extension) 
            ret.insert(it->path());
        }
      }
      return ret;
    }

    auto
    lsf(const std::string&extension) const
    {
      return lsf(wd,extension);
    }

    // list directories
    std::vector<boost::filesystem::path>
    lsd(const boost::filesystem::path&dir) 
    {
      std::vector<boost::filesystem::path> ret;
      auto p=adjust_path(dir);
      ASSERT(status(p)==DIRECTORY,"not a directory");
      for (auto it=boost::filesystem::directory_iterator(p);
           it!=boost::filesystem::directory_iterator();
           ++it) 
      {
        boost::system::error_code ec;
        if (it->status(ec).type()==boost::filesystem::file_type::directory_file) 
          ret.push_back(it->path());
      }
      return ret;
    }

    // list files recursively
    std::set<boost::filesystem::path>
    lsr(const boost::filesystem::path&dir,const std::string&extension)
    {
      auto files=lsf(dir,extension);
      auto dirs=lsd(dir);
      FOR(const auto&d,dirs)
      {
        auto files_r=lsr(d,extension);
        FOR(const auto&f,files_r) {files.insert(f);}
      }
      return files;
    }

  };

}
