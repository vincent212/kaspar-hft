# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Coinbase Exchange FIX order manager (not certified): CbxOM, SimCbx, the
# QuickFIX session, and the unit tests. Not wired into the top-level build.
# Needs the kaspar libraries (frame, logger, mq0, chutil, actors), Boost,
# OpenSSL, ZeroMQ and Google Test; the QuickFIX session also needs a
# QuickFIX built with SSL (QUICKFIX_PATH with include/quickfix and
# libquickfix.a). Without QUICKFIX_PATH the OM, the simulator and the tests
# are built and the real session is left out (quickfix_session_factory()
# returns nullptr).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test
#   make test QUICKFIX_PATH=/path/to/quickfix
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
QUICKFIX_PATH ?=
CXX ?= c++

MD = $(KSPRPROJ)/global_connectivity/non_certified/market_data/coinbase_exchange
BOOK = $(KSPRPROJ)/frame_kaspr/src/BrokerOrderBook.cpp
SRC = src/CbxOM.cpp src/CbxConfig.cpp src/CbxSessionUtil.cpp src/SimCbx.cpp src/QfSession.cpp $(MD)/src/Rest.cpp
TESTS = $(wildcard tests/test_*.cpp)

QF_FLAGS = $(if $(QUICKFIX_PATH),-DCBX_WITH_QUICKFIX -DHAVE_SSL=1 -I$(QUICKFIX_PATH)/include,)
QF_LIBS = $(if $(QUICKFIX_PATH),$(QUICKFIX_PATH)/libquickfix.a,)

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -Iinclude -Itests -I$(MD)/include -I$(KSPRPROJ)/sim/include $(INCL) -I$(GTEST_PATH)/include \
        -I$(OPENSSL_PATH)/include $(QF_FLAGS)
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) $(QF_LIBS) \
       -L$(OPENSSL_PATH)/lib -lssl -lcrypto \
       -L$(GTEST_PATH)/lib -lgtest -lgtest_main \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

OBJ = $(patsubst %.cpp,build/obj/%.o,$(notdir $(SRC) $(TESTS))) build/obj/BrokerOrderBook.o
vpath %.cpp src tests $(MD)/src

.PHONY: all test clean
all: build/run_cbx_om_tests

build/obj/%.o: %.cpp $(wildcard include/coinbase_exchange_oe/*.hpp include/coinbase_exchange_oe/*/*.hpp tests/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/BrokerOrderBook.o: $(BOOK)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_cbx_om_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS)

test: build/run_cbx_om_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_cbx_om_tests

clean:
	rm -rf build
