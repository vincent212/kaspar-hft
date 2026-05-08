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

# External dependencies - OS-specific paths
ifeq ($(UNAME_S),Darwin)
    BOOST_PATH=/opt/homebrew/opt/boost
    ZLIB_PATH=/opt/homebrew/opt/zlib
    GSL_PATH=/opt/homebrew/opt/gsl
    CPPZMQ_PATH=/opt/homebrew/opt/cppzmq
    ZMQ_PATH=/opt/homebrew/opt/zeromq
    JSON_PATH=/opt/homebrew/opt/nlohmann-json
    CRYPTOPP_PATH=/opt/homebrew/opt/cryptopp
else
    BOOST_PATH=/home/vmayeski/local/boost190
    ZLIB_PATH=/usr
    GSL_PATH=/usr/local/gsl
    CPPZMQ_PATH=/usr/local
    ZMQ_PATH=/usr/local
    JSON_PATH=/home/vmayeski/local
    CRYPTOPP_PATH=/home/vmayeski/local
endif

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
-I/home/vmayeski/local/include/mysql \
-I$(INSTALL_PATH)

MFLAGS=$(INCL) $(EXTRA_INCL) $(STANDARD) $(CFLAGS_SPEC)
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
# TIMTRACE enables timing logs and assertions - comment out for simulation
#DEFINES_OPT=-DCONSTR_NO_CHECK_NAN -fno-plt -DTIMTRACE
DEFINES_OPT=-DCONSTR_NO_CHECK_NAN -fno-plt
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

    # Linux link flags (includes MySQL for position persistence)
    LDFLAGS_COMMON = -L$(BOOST_PATH)/lib -L$(ZLIB_PATH)/lib -L$(GSL_PATH)/lib -L/usr/local/lib -L/usr/local/lib64 -L/home/vmayeski/local/lib/mariadb -L/home/vmayeski/local/lib -lboost_thread -lboost_filesystem -lpthread -lzmq -lmysqlclient
endif

LDFLAGS_OPT = $(LDFLAGS_COMMON) -Wl,-rpath,/usr/local/lib64:/usr/local/lib:$(BOOST_PATH)/lib:/home/vmayeski/local/lib:/home/vmayeski/local/lib/mariadb:/usr/local/gsl/lib
LDFLAGS_DBG = $(LDFLAGS_COMMON) -Wl,-rpath,/usr/local/lib64:/usr/local/lib:$(BOOST_PATH)/lib:/home/vmayeski/local/lib:/home/vmayeski/local/lib/mariadb:/usr/local/gsl/lib

# Common library definitions for kaspr applications
# Actors library path
ACTORS_LIB_PATH = $(KSPRPROJ)/actors/cpp

# Standard libraries for optimized builds
LIBS_COMMON_OPT = -lpositionman -lsuper -lmtd -ldb -llight -lframe -lmdp3 -lmcast_recv -lilink -lmq0 -llogger -lchutil $(ACTORS_LIB_PATH)/libactors.a -lgsl -lgslcblas

# Standard libraries for debug builds
LIBS_COMMON_DBG = -lpositionmang -lsuperg -lmtdg -ldbg -llightg -lframeg -lmdp3g -lmcast_recvg -lilinkg -lmq0g -lloggerg -lchutilg $(ACTORS_LIB_PATH)/libactorsg.a -lgsl -lgslcblas

# Default LIBSO/LIBSG for applications (can be appended with +=)
# and -lbacktrace are Linux/GCC-only
ifeq ($(UNAME_S),Darwin)
    LIBSO = $(LIBS_COMMON_OPT) -lboost_program_options -lz
    LIBSG = $(LIBS_COMMON_DBG) -lboost_program_options -lz
else
    LIBSO = $(LIBS_COMMON_OPT) -lboost_program_options -lz -lbacktrace
    LIBSG = $(LIBS_COMMON_DBG) -lboost_program_options -lz -lbacktrace
endif
