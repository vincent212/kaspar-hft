#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "chutil/Macros.hpp"
#include <float.h>
#include <boost/lexical_cast.hpp>
#include <unistd.h>
#include <string>
#include <string.h>

#define FLTT double

namespace chutil
{

    typedef struct
    {
        FLTT max;
        int max_idx;
    } max_in_d_arr_t;

    typedef struct
    {
        FLTT min;
        int min_idx;
    } min_in_d_arr_t;

    INLINE double
    to_double(const char *str)
    {
        return boost::lexical_cast<double>(str);
    }

    INLINE float
    to_float(const char *str)
    {
        return boost::lexical_cast<float>(str);
    }

    INLINE int
    to_int(const char *str)
    {
        return boost::lexical_cast<int>(str);
    }

    INLINE uint
    to_uint(const char *str)
    {
        return boost::lexical_cast<uint>(str);
    }

    INLINE long long
    to_long(const char *str)
    {
        return boost::lexical_cast<long long>(str);
    }

    INLINE unsigned long long
    to_ulong(const char *str)
    {
        return boost::lexical_cast<unsigned long long>(str);
    }

    INLINE double
    to_double(const std::string &str)
    {
        return boost::lexical_cast<double>(str);
    }

    INLINE float
    to_float(const std::string &str)
    {
        return boost::lexical_cast<float>(str);
    }

    INLINE int
    to_int(const std::string &str)
    {
        return boost::lexical_cast<int>(str);
    }

    INLINE uint
    to_uint(const std::string &str)
    {
        return boost::lexical_cast<uint>(str);
    }

    INLINE long long
    to_long(const std::string &str)
    {
        return boost::lexical_cast<long long>(str);
    }

    INLINE unsigned long long
    to_ulong(const std::string &str)
    {
        return boost::lexical_cast<unsigned long long>(str);
    }

    // Parse YYYYMMDD char array to uint32_t (e.g., "20251215" -> 20251215)
    INLINE uint32_t
    parse_yyyymmdd(const char* date_str, size_t len)
    {
        if (len < 8)
            return 0;
        uint32_t result = 0;
        for (size_t i = 0; i < 8; i++)
        {
            char c = date_str[i];
            if (c < '0' || c > '9')
                return 0;
            result = result * 10 + (c - '0');
        }
        return result;
    }

    INLINE char *
    ut_parse_date(const char RPTR input, const char RPTR fmt, struct tm RPTR tm, bool check_for_null = true)
    {
        char *cp;
        memset(tm, '\0', sizeof(*tm));
        cp = strptime(input, fmt, tm);
        if (check_for_null && cp == NULL)
        {
            fprintf(stderr, "could not parse %s using %s\n", input, fmt);
            ABORT("parse error");
        }
        return cp;
    }

    static inline char *
    ut_parse_date_2(const char RPTRM date_str,
                    const char RPTRM tim_str,
                    struct tm *tm, const char RPTR fmt)
    {

        char buf[BUF_LEN];
        int i = 0;
        while (*date_str != 0)
            buf[i++] = *date_str++;
        buf[i++] = ':';
        while (*tim_str != 0)
            buf[i++] = *tim_str++;
        buf[i] = 0;

        if (i > BUF_LEN)
        {
            fprintf(stderr, "ut_parse_date_1: buffer overrun\n");
            ABORT("parse error");
        }

        return ut_parse_date(buf, fmt, tm);
    }

    [[maybe_unused]] static time_t
    ut_parse_date_time_2(const char RPTRM date_str,
                         const char RPTRM tim_str, const char RPTR fmt)
    {
        struct tm tm;

        char buf[512];
        char *buf_ = buf;
        int i = 0;
        while (*date_str != 0)
        {
            *buf_++ = *date_str++;
            i++;
        }
        *buf_++ = ':';
        while (*tim_str != 0)
        {
            *buf_++ = *tim_str++;
            i++;
        }
        *buf_ = 0;

        if (i + 1 > 512)
        {
            fprintf(stderr, "ut_parse_date_time_2: buffer overrun\n");
            ABORT("parse error");
        }

        ut_parse_date(buf, fmt, &tm);

        time_t ret = mktime(&tm);
        return ret;
    }

    INLINE char *
    ut_clean_string(char *str)
    {
        char *strCpy = str;
        while (*strCpy)
        {
            if (*strCpy == '\r')
            {
                *strCpy = '\0';
                break;
            }
            else if (*strCpy == '\n')
            {
                *strCpy = '\0';
                break;
            }
            strCpy++;
        }
        return str;
    }

    INLINE char *
    ut_trim(char *str)
    {
        char *strCpy = str;
        while (*strCpy)
        {
            if (*strCpy == ' ' || *strCpy == '\r' || *strCpy == '\n')
            {
                *strCpy = 0;
                return str;
            }
            strCpy++;
        }
        return str;
    }


    INLINE void
    ut_writerow(FILE *of, char **outBuf, int len)
    {
        int i;
        for (i = 0; i < len - 1; i++)
            fprintf(of, "%s,", outBuf[i]);
        fprintf(of, "%s\n", outBuf[len - 1]);
    }

    INLINE void
    ut_timds(int td, char *buf)
    {

        int d = td / (3600 * 24);
        td -= d * (3600 * 24);
        int h = td / 3600;
        td -= h * 3600;
        int m = td / 60;
        td -= m * 60;
        int s = td;

        sprintf(buf, "%3d:%02d:%02d:%02d", d, h, m, s);
    }


    INLINE void
    ut_time_t_to_string(time_t tim, int ms, char *buf, int hide = 0)
    {

        struct tm *tm = localtime(&tim);

        int y = 1900 + tm->tm_year;
        int m = tm->tm_mon + 1;
        int d = tm->tm_mday;
        int h = tm->tm_hour;
        int mi = tm->tm_min;
        int s = tm->tm_sec;

        if (hide == 1)
        {
            sprintf(buf, "%4d%02d%02d:", y, m, d);
        }
        else if (hide == 0)
        {
            sprintf(buf, "%d-%02d-%02d:%02d:%02d:%02d.%03d", y, m, d, h, mi, s, ms);
        }
        else if (hide == 2)
        {
            sprintf(buf, "%d%02d%02d.%02d%02d%02d.%03d", y, m, d, h, mi, s, ms);
        }
    }

    INLINE std::string
    ut_time_t_to_string(time_t tim, int ms)
    {
        char buf[200];
        ut_time_t_to_string(tim, ms, buf);
        return std::string(buf);
    }

    INLINE void
    ut_time_t_parse(time_t tim,
                    int *y,
                    int *mon,
                    int *d, int *h, int *m, int *s)
    {
        struct tm *tm = localtime(&tim);
        *y = 1900 + tm->tm_year;
        *mon = tm->tm_mon + 1;
        *d = tm->tm_mday;
        *h = tm->tm_hour;
        *m = tm->tm_min;
        *s = tm->tm_sec;
    }

    INLINE void
    ut_copy_d_arr(FLTT *dest,
                  const FLTT RPTRM src, int len)
    {
        int i;
        for (i = 0; i < len; i++)
        {
            *dest = *src;
            dest++;
            src++;
        }
    }

    INLINE void
    ut_unique_string(char *buf)
    {
        int pid = getpid();
        time_t tim = time(0);
        sprintf(buf, "%d.%d.%s", int(tim), pid, "tmp");
    }

    INLINE int
    ut_zeroXX(int x)
    {
        int dig1 = x % 10;
        int dig2 = (x / 10) % 10;
        return x - dig2 * 10 - dig1;
    }

    INLINE void
    ut_shift_left(int *arr, int len)
    {
        for (int i = 0; i < len - 1; i++)
            arr[i] = arr[i + 1];
        arr[len - 1] = 0;
    }

    INLINE void
    ut_shift_right(int *arr, int len)
    {
        for (int i = len - 1; i > 0; i--)
            arr[i] = arr[i - 1];
        arr[0] = 0;
    }

    INLINE int
    ut_int_index_of(const int *arr, int len, int val)
    {
        for (int i = 0; i < len; i++)
            if (arr[i] == val)
                return i;
        return -1;
    }

    INLINE int
    ut_int_index_of_lte(const int *arr, int len, int val)
    {
        for (int i = 0; i < len; i++)
            if (arr[i] <= val)
                return i;
        return -1;
    }

    INLINE int
    ut_int_index_of_gte(const int *arr, int len, int val)
    {
        for (int i = 0; i < len; i++)
            if (arr[i] <= val)
                return i;
        return -1;
    }

    INLINE char
    ut_digit_to_char(int digit)
    {
        switch (digit)
        {
        case 0:
            return '0';
        case 1:
            return '1';
        case 2:
            return '2';
        case 3:
            return '3';
        case 4:
            return '4';
        case 5:
            return '5';
        case 6:
            return '6';
        case 7:
            return '7';
        case 8:
            return '8';
        case 9:
            return '9';
        default:
            ASSERT(0, "not a digit");
        }
        abort();
        return 0;
    }

    INLINE char *
    ut_form_time_str(int hour, int min)
    {
        static char buf[32];
        memset(buf, 0, 32);
        sprintf(buf, "%02d:%02d:00", hour, min);
        return buf;
    }

    /*
      for n = 0 to N - 2
      for m = n + 1 to N - 1
      swap A(n,m) with A(m,n)
    */

    INLINE void
    ut_transpose(FLTT **mat, int N)
    {
        for (int n = 0; n < N - 1; n++)
        {
            for (int m = n + 1; m < N; m++)
            {
                FLTT tmp = mat[n][m];
                mat[n][m] = mat[m][n];
                mat[m][n] = tmp;
            }
        }
    }

    INLINE time_t
    ut_taqtime_to_unixtime(int taqtime, int day, int mon, int year)
    {
        int noms = taqtime / 1000;
        int nos = noms / 100;
        int nom = nos / 100;
        int s = noms % 100;
        int m = nos % 100;
        int h = nom % 100;
        struct tm tm;
        memset(&tm, 0, sizeof(struct tm));
        tm.tm_sec = s;
        tm.tm_min = m;
        tm.tm_hour = h;
        tm.tm_mday = day;
        tm.tm_mon = mon - 1;
        tm.tm_year = year - 1900;
        time_t ret = mktime(&tm);
        return ret;
    }

    INLINE time_t
    ut_to_unixtime(int year, int mon, int day, int h, int m, int s)
    {
        struct tm tm;
        memset(&tm, 0, sizeof(struct tm));
        tm.tm_sec = s;
        tm.tm_min = m;
        tm.tm_hour = h;
        tm.tm_mday = day;
        tm.tm_mon = mon - 1;
        tm.tm_year = year - 1900;
        time_t ret = mktime(&tm);
        return ret;
    }

    INLINE char *
    ut_backslashify(const char *fname)
    {
        static char buf[2000];
        int cnt = 0;
        for (unsigned i = 0; i < strlen(fname); i++)
        {
            if (fname[i] == ' ')
            {
                buf[cnt++] = '\\';
                buf[cnt++] = ' ';
            }
            else
                buf[cnt++] = fname[i];
        }
        return buf;
    }

}

#define MAXPENDING 100 /* Maximum outstanding socket connection requests */

