
#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <iostream>
#include <memory>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <algorithm>
#include <typeinfo>
#include <typeindex>
#include <iostream>

// Assert.hpp must be ina separate file
// so it can be used standalone
#include "chutil/Assert.hpp"

#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <boost/current_function.hpp>
#include <boost/format.hpp>


#define CHTYPEID(x) std::type_index(typeid(x))

// for resticted pointers
#define RPTR *const
#define RPTRM *

#define INLINE __attribute__((always_inline)) static inline

#define PRINT(msg) \
  {fprintf (stderr,"%s\n",msg);						\
    fprintf (stderr,"in %s %s %d\n",__FUNCTION__,__FILE__,__LINE__);	\
    printf ("%s\n",msg);						\
    printf ("in %s %s %d\n",__FUNCTION__,__FILE__,__LINE__);		\
    fflush (stdout);}


#define ABORT(msg) {PRINT("ABORT! "); \
  char ____buf[2000]; \
  sprintf(____buf, "%s %s %d", msg, __FILE__, __LINE__); \
  fprintf(stderr, "%s", ____buf); \
  fprintf(stdout, "%s", ____buf); \
  fflush(stdout); \
  PRINT_ARGS; \
  abort(); \
}

#define ERR(msg) \
  {ASSERT(0, msg);}

#define ERRF(bfmsg) \
  {ASSERTF(0, bfmsg);}

INLINE FILE *
openf (const char *name, const char *type)
{
  FILE *ret = fopen (name, type);
  if (!ret)
    {
      printf ("could not open %s\n", name);
      ABORT("could not open file");
    }
  ASSERT (!feof (ret), "end of file on open?");
  if (ferror (ret))
    {
      perror ("file has errors on open");
      ABORT("could not open file");
    }
  return ret;
}

#define SNGH ASSERT(0,"should not get here")

#define BUF_LEN 2048

#include <boost/foreach.hpp>
#define FOR BOOST_FOREACH
#define RFOR BOOST_REVERSE_FOREACH

#define DO_PRAGMA(x) _Pragma (#x)
#define TODO(x) DO_PRAGMA(message ("TODO - " #x))
// TODO(Remember to fix this)

