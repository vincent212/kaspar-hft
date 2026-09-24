# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

#
# global defs for m2_kaspr (actors-based build)
#

STANDARD=-std=gnu++20

# OS-specific CFLAGS_SPEC
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS/ARM: no -mcx16 (x86-only)
    CFLAGS_SPEC= -pipe -ffunction-sections -fdata-sections
else
    # Linux/x86
    CFLAGS_SPEC= -pipe -ffunction-sections -fdata-sections -mcx16
endif

INSTALL_PATH=$(KSPRPROJ)

# Core paths for kaspr
ACTORS_PATH=$(INSTALL_PATH)/actors/cpp
CHUTIL_PATH=$(INSTALL_PATH)/chutil
FRAME_PATH=$(INSTALL_PATH)/frame_kaspr
LIGHT_PATH=$(INSTALL_PATH)/light
ILINK_PATH=$(INSTALL_PATH)/ilink
MDP3_PATH=$(INSTALL_PATH)/mdp3
MCAST_RECV_PATH=$(INSTALL_PATH)/mcast_recv
MTD_PATH=$(INSTALL_PATH)/mtd
LOGGER_PATH=$(INSTALL_PATH)/logger
OOGSL_PATH=$(INSTALL_PATH)/oogsl
DB_PATH=$(INSTALL_PATH)/db
MQ0_PATH=$(INSTALL_PATH)/mq0
SUPER_PATH=$(INSTALL_PATH)/super
AHEDGE_PATH=$(INSTALL_PATH)/ahedge
POSITIONMAN_PATH=$(INSTALL_PATH)/positionman
FRAME_REF_PATH=$(INSTALL_PATH)/frame_ref

# OS detection
UNAME_S := $(shell uname -s)

# External dependency prefixes — overridable from the environment (`?=`, so a
# shell `export` wins over the default). Defaults match a vanilla system
# install; set overrides in your shell profile — or run mk_kaspr/detect_paths.sh
# to auto-detect them — when the libs live under a home-dir prefix. See
# mk_kaspr/PATHS.md.
ifeq ($(UNAME_S),Darwin)
    BOOST_PATH    ?= /opt/homebrew/opt/boost
    ZLIB_PATH     ?= /opt/homebrew/opt/zlib
    GSL_PATH      ?= /opt/homebrew/opt/gsl
    CPPZMQ_PATH   ?= /opt/homebrew/opt/cppzmq
    ZMQ_PATH      ?= /opt/homebrew/opt/zeromq
    JSON_PATH     ?= /opt/homebrew/opt/nlohmann-json
    CRYPTOPP_PATH ?= /opt/homebrew/opt/cryptopp
    GTEST_PATH    ?= $(firstword $(foreach p,/opt/homebrew/opt/googletest $(HOME)/local /usr/local,\
                       $(if $(wildcard $(p)/include/gtest/gtest.h),$(p))) /opt/homebrew/opt/googletest)
else
    BOOST_PATH    ?= /usr/local
    ZLIB_PATH     ?= /usr
    GSL_PATH      ?= /usr/local/gsl
    CPPZMQ_PATH   ?= /usr/local
    ZMQ_PATH      ?= /usr/local
    JSON_PATH     ?= /usr/local
    CRYPTOPP_PATH ?= /usr/local
    # gtest has no distro package on this box -- it is a home-dir install. Probe
    # for the header instead of hardcoding a prefix, so `make test` works without
    # having run `eval "$(mk_kaspr/detect_paths.sh)"` first. That eval finds it
    # too; this is the fallback for a shell that has not been through it.
    GTEST_PATH    ?= $(firstword $(foreach p,$(HOME)/local /usr/local /usr,\
                       $(if $(wildcard $(p)/include/gtest/gtest.h),$(p))) /usr)
endif

# Extra -L / rpath root for home-dir installs (Linux links -L$(LOCAL_LIB_PATH)/lib).
LOCAL_LIB_PATH ?= /usr/local

INCL=\
-I$(ACTORS_PATH)/include \
-I$(CHUTIL_PATH)/include \
-I$(FRAME_PATH)/include \
-I$(FRAME_REF_PATH)/include \
-I$(LIGHT_PATH)/include \
-I$(ILINK_PATH)/include \
-I$(MDP3_PATH)/include \
-I$(MCAST_RECV_PATH)/include \
-I$(MTD_PATH)/include \
-I$(LOGGER_PATH)/include \
-I$(OOGSL_PATH)/include \
-I$(DB_PATH)/include \
-I$(MQ0_PATH)/include \
-I$(SUPER_PATH)/include \
-I$(SUPER_PATH) \
-I$(AHEDGE_PATH)/include \
-I$(POSITIONMAN_PATH)/include \
-I$(INSTALL_PATH)/interface \
-I$(BOOST_PATH)/include \
-I$(ZLIB_PATH)/include \
-I$(GSL_PATH)/include \
-I$(CPPZMQ_PATH)/include \
-I$(ZMQ_PATH)/include \
-I$(JSON_PATH)/include \
-I$(CRYPTOPP_PATH)/include \
-I$(INSTALL_PATH)

# MFLAGS drives the `$(CC) -M` scan that generates the .P dependency files.
# It MUST carry the same -D flags as the real compile: a header reached only
# through an #ifdef is invisible to a scan run without that define, so it never
# lands in the .P, and editing it then rebuilds NOTHING while make reports
# success.
#
# That is not hypothetical. kaspr.hpp includes frame/ob/act/TachBook.hpp under
# #ifdef USE_TACHBOOK, which kaspr/src/Makefile adds to DEFINES_COMMON. Without
# the line below, kaspr.P listed no TachBook.hpp at all, so edits to the order
# book silently relinked a stale object -- a green build running old code.
#
# DEFINES_COMMON is the right variable: it is the one per-directory Makefiles
# append to (`DEFINES_COMMON += -DUSE_TACHBOOK`), and `=` here is recursive, so
# those appends are picked up at use time even though this line comes first.
MFLAGS=$(INCL) $(EXTRA_INCL) $(STANDARD) $(CFLAGS_SPEC) $(DEFINES_COMMON)
CFLAGS=$(INCL) $(EXTRA_INCL) $(STANDARD) $(CFLAGS_SPEC)

# OS-specific warnings
# -Wno-stringop-truncation, -Wno-volatile are GCC-only (not clang/macOS)
ifeq ($(UNAME_S),Darwin)
    WARNINGS = -W -Wextra -Wall -Wno-reorder -Wpedantic \
    -Wno-int-in-bool-context -Wno-unused-function \
    -Wno-deprecated-declarations -Werror=odr
else
    WARNINGS = -W -Wextra -Wall -Wno-reorder -Wpedantic \
    -Wno-int-in-bool-context -Wno-unused-function -Wno-stringop-truncation \
    -Wno-deprecated-declarations -Wno-volatile -Werror=odr
endif

DEFINES_COMMON=-fPIC

# ---- dual-path decode verification tee -------------------------------------
#
# OFF unless asked for:  export VERIFY_TEE=1  before build.sh (or `make
# VERIFY_TEE=1` in every directory). Nothing in a default build contains the
# tee -- not the member, not the branch, not the extra constructor parameter.
#
# This MUST be global and cannot be a per-directory define like USE_TACHBOOK.
# mdp3::MessageProcessor is compiled into libmdp3 (mdp3/src/message_processor.cpp)
# while the wiring that hands it a shadow decoder lives in kaspr/src/kaspr.cpp.
# Define it in only one of those two and the class has a different member
# layout and a different constructor signature in each translation unit. That
# is an ODR violation which LINKS CLEANLY and corrupts at run time -- the exact
# failure mode -Werror=odr above cannot see across a static library.
#
# Export it, do not pass it on the command line: build.sh recurses into each
# directory with a fresh make, and a command-line variable is not inherited.
#
# NOTE: no .P dependency file tracks glob_begin.mk (see DEFINES_OPT below), so
# flipping this rebuilds NOTHING. Force a full recompile and md5sum the binary
# before trusting that the tee is in -- or out -- of what you just built.
ifdef VERIFY_TEE
DEFINES_COMMON += -DMDP3_VERIFY_TEE
endif

# TIMTRACE enables timing logs and assertions - comment out for simulation
#DEFINES_OPT=-DCONSTR_NO_CHECK_NAN -DPLACES_NO_CHECK_CONSTRAINT -fno-plt -DTIMTRACE
#
# PLACES_NO_CHECK_CONSTRAINT compiles out the place<> guards in opt. It covers
# more than the write-once check: get()/getr() lose their read-before-write
# guard too, and that one is on the hot path (every config read per message),
# which is where the win is. The cost is that an unassigned field returns
# uninitialised memory instead of aborting. Debug builds keep both.
#
# So: after any change that touches a place<> field, run the workload in DEBUG
# first and confirm the guards stay silent. Compiling out a check you have not
# seen pass is how a config field that is written twice -- or read before it is
# written -- turns into a plausible wrong number instead of an abort. The whole
# point of the guard is that it fires once, in debug, on your machine.
#
# FAST_SZ_BOOK drops the O(level) verification walks in OrderQ. Without it
# get_orders_in_book() walks the whole price level on EVERY call and asserts the
# running counters against what it finds; size_of_book() does the same. They are
# debug checks that were being paid for in opt, on a path the book hits
# constantly, and a busy ES level is not short.
#
# Measured on one ES session, byte-identical output either way:
#
#     without   110.43 s
#     with       61.27 s      1.80x
#
# The counters themselves (orders_in_book, size_of_book_cnt) are maintained the
# same way in both builds -- this only removes the walk that re-derives and
# checks them. Debug builds keep it, which is where a counter that has drifted
# will still abort.
#
# NOTE: no .P dependency file tracks glob_begin.mk, so editing this line
# rebuilds NOTHING and make reports success. After changing it, force a
# recompile (touch the sources, or ./build.sh -f on branches that have it) and
# md5sum the binaries before trusting any A/B.
DEFINES_OPT=-DCONSTR_NO_CHECK_NAN -DPLACES_NO_CHECK_CONSTRAINT -DFAST_SZ_BOOK -fno-plt
DEFINES_DBG=-DNOINLINE

# OS-specific build flags
ifeq ($(UNAME_S),Darwin)
    # macOS optimized build flags (no -Wl,--gc-sections, -mfpmath=sse, -mcx16, -rdynamic)
    CFLAGS_OPT = -march=native -O3 -ggdb $(DEFINES_COMMON) $(DEFINES_OPT) $(WARNINGS) -flto -fdata-sections -ffunction-sections -mtune=native -fno-plt

    # macOS debug build flags
    CFLAGS_DBG = -O0 -DDEBUG -DLOGDEBUG -ggdb $(DEFINES_COMMON) $(DEFINES_DBG) $(WARNINGS)

    # macOS link flags
    LDFLAGS_COMMON = -L$(BOOST_PATH)/lib -L$(ZLIB_PATH)/lib -L$(GSL_PATH)/lib -L$(CRYPTOPP_PATH)/lib -L$(ZMQ_PATH)/lib -lboost_system -lboost_thread -lboost_filesystem -lpthread -lzmq -lcryptopp
else
    # Linux optimized build flags
    CFLAGS_OPT = -march=native -O3 -ggdb $(DEFINES_COMMON) $(DEFINES_OPT) $(WARNINGS) -rdynamic -flto -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-rpath,/usr/local/lib64:/usr/local/lib:$(BOOST_PATH)/lib:/usr/local/zlib/lib:/usr/local/gsl/lib -mfpmath=sse -mtune=native -flto=16 -mcx16 -fno-plt

    # Linux debug build flags (need -mavx2 for SIMD intrinsics in headers)
    CFLAGS_DBG = -O0 -DDEBUG -DLOGDEBUG -ggdb $(DEFINES_COMMON) $(DEFINES_DBG) $(WARNINGS) -rdynamic -Wl,-rpath,/usr/local/lib64:/usr/local/lib:$(BOOST_PATH)/lib:/usr/local/zlib/lib:/usr/local/gsl/lib -mcx16 -mavx2

    # Linux link flags
    LDFLAGS_COMMON = -L$(BOOST_PATH)/lib -L$(ZLIB_PATH)/lib -L$(GSL_PATH)/lib -L/usr/local/lib -L/usr/local/lib64 -L$(LOCAL_LIB_PATH)/lib -lboost_thread -lboost_filesystem -lpthread -lzmq
endif

LDFLAGS_OPT = $(LDFLAGS_COMMON) -Wl,-rpath,/usr/local/lib64:/usr/local/lib:$(BOOST_PATH)/lib:$(LOCAL_LIB_PATH)/lib:/usr/local/gsl/lib
LDFLAGS_DBG = $(LDFLAGS_COMMON) -Wl,-rpath,/usr/local/lib64:/usr/local/lib:$(BOOST_PATH)/lib:$(LOCAL_LIB_PATH)/lib:/usr/local/gsl/lib

# Common library definitions for kaspr applications
# Actors library path
ACTORS_LIB_PATH = $(KSPRPROJ)/actors/cpp

# Standard libraries for optimized builds
LIBS_COMMON_OPT = -lpositionman -lmtd -ldb -llight -lframe -lmdp3 -lmcast_recv -lilink -lmq0 -llogger -lchutil $(ACTORS_LIB_PATH)/libactors.a -lgsl -lgslcblas

# Standard libraries for debug builds
LIBS_COMMON_DBG = -lpositionmang -lmtdg -ldbg -llightg -lframeg -lmdp3g -lmcast_recvg -lilinkg -lmq0g -lloggerg -lchutilg $(ACTORS_LIB_PATH)/libactorsg.a -lgsl -lgslcblas

# Default LIBSO/LIBSG for applications (can be appended with +=)
# and -lbacktrace are Linux/GCC-only
ifeq ($(UNAME_S),Darwin)
    LIBSO = $(LIBS_COMMON_OPT) -lboost_program_options -lz
    LIBSG = $(LIBS_COMMON_DBG) -lboost_program_options -lz
else
    LIBSO = $(LIBS_COMMON_OPT) -lboost_program_options -lz -lbacktrace
    LIBSG = $(LIBS_COMMON_DBG) -lboost_program_options -lz -lbacktrace
endif
