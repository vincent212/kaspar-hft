# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# LMAX Broker Trading order manager over FIX 4.4 (not certified): unit tests,
# the loopback tests against SimLmaxGateway and the live check tool. Not part
# of the top-level build. Needs the kaspar libraries (frame, logger, mq0,
# chutil, actors), Boost, ZeroMQ, OpenSSL, Google Test and a static QuickFIX
# built with HAVE_SSL (QF_DIR/include/quickfix/*.h, QF_DIR/libquickfix.a):
#
#   export KSPRPROJ=~/kaspar-hft
#   make test QF_DIR=/path/to/qfbuild
#   make lmax_om_check QF_DIR=...        # live check tool (needs a key file to run)
#   make dict SPEC=/path/broker-trading.fix-spec.json   # regenerate and diff
#
# The shared FIX code (environment guard, redacting log and store, the
# initiator, the instrument file, the simulator, and for lmax_om_check the
# market-data feed) is compiled in from ../../market_data/lmax.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
QF_DIR     ?= $(HOME)/local/quickfix
OPENSSL    ?= /opt/homebrew/opt/openssl@3
CXX        ?= c++
PYTHON     ?= python3

MD = $(abspath $(HERE)/../../market_data/lmax)
DICT = $(HERE)/dict/LMAX-FIX44-Trading.xml
OBJDIR = $(HERE)/build/obj

LIB_SRC = src/ExecParse.cpp src/QfLmaxSession.cpp src/LmaxOM.cpp src/LmaxOMConfig.cpp
MD_SRC = LmaxFix.cpp RedactLog.cpp FixInitiator.cpp MdParse.cpp SimLmaxGateway.cpp LmaxMdSync.cpp LmaxMdFeed.cpp
TEST_SRC = tests/test_exec_parse.cpp tests/test_lmax_om.cpp tests/test_qf_om.cpp
BOOK = $(KSPRPROJ)/frame_kaspr/src/BrokerOrderBook.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(HERE)/tests -I$(MD)/include -I$(MD)/tests -I$(KSPRPROJ)/sim/include \
        -I$(QF_DIR)/include $(INCL) \
        -I$(OPENSSL)/include -I$(GTEST_PATH)/include -DHAVE_SSL=1 \
        -DLMAX_TRADING_DICT='"$(DICT)"' -DLMAX_MD_DICT='"$(MD)/dict/LMAX-FIX44-MarketData.xml"'
QF_LIBS = $(QF_DIR)/libquickfix.a -L$(OPENSSL)/lib -lssl -lcrypto
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) $(QF_LIBS) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

LIB_OBJ = $(patsubst %.cpp,$(OBJDIR)/%.o,$(LIB_SRC)) $(patsubst %.cpp,$(OBJDIR)/md/%.o,$(MD_SRC)) \
          $(OBJDIR)/BrokerOrderBook.o $(OBJDIR)/sim/Mbp2Mbo.o $(OBJDIR)/sim/LevelWindow.o
TEST_OBJ = $(patsubst %.cpp,$(OBJDIR)/%.o,$(TEST_SRC))

.PHONY: all test dict lmax_om_check clean
all: build/run_lmax_om_tests build/lmax_om_check

$(OBJDIR)/%.o: $(HERE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

$(OBJDIR)/md/%.o: $(MD)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

$(OBJDIR)/sim/%.o: $(KSPRPROJ)/sim/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

$(OBJDIR)/BrokerOrderBook.o: $(BOOK)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

build/liblmax_om.a: $(LIB_OBJ)
	ar rcs $@ $^

build/run_lmax_om_tests: $(TEST_OBJ) build/liblmax_om.a
	$(CXX) $(TEST_OBJ) build/liblmax_om.a -o $@ -L$(GTEST_PATH)/lib -lgtest -lgtest_main $(LIBS)

build/lmax_om_check: $(OBJDIR)/tools/lmax_om_check.o build/liblmax_om.a
	$(CXX) $^ -o $@ $(LIBS)

lmax_om_check: build/lmax_om_check

test: build/run_lmax_om_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) ./build/run_lmax_om_tests

dict:
	$(PYTHON) $(MD)/tools/fixspec2qf.py $(SPEC) --check
	@mkdir -p build
	$(PYTHON) $(MD)/tools/fixspec2qf.py $(SPEC) -o build/LMAX-FIX44-Trading.xml
	diff -u $(DICT) build/LMAX-FIX44-Trading.xml && echo "dictionary unchanged"

clean:
	rm -rf build

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
