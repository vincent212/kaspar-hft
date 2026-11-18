# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# LMAX Broker Market Data over FIX 4.4 (not certified): unit tests, the
# loopback simulator test and the live check tool. Not part of the top-level
# build. Needs the kaspar libraries (frame, logger, mq0, chutil, actors),
# Boost, ZeroMQ, OpenSSL, Google Test and a static QuickFIX built with
# HAVE_SSL (QF_DIR/include/quickfix/*.h and QF_DIR/libquickfix.a):
#
#   export KSPRPROJ=~/kaspar-hft
#   make test QF_DIR=/path/to/qfbuild
#   make lmax_md_check QF_DIR=...     # live check tool (needs a key file to run)
#   make dict SPEC=/path/broker-market-data.fix-spec.json   # regenerate and diff
#
# sim/src/Mbp2Mbo.cpp and LevelWindow.cpp are compiled in from KSPRPROJ.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
QF_DIR     ?= $(HOME)/local/quickfix
OPENSSL    ?= /opt/homebrew/opt/openssl@3
CXX        ?= c++
PYTHON     ?= python3

DICT = $(HERE)/dict/LMAX-FIX44-MarketData.xml
OBJDIR = $(HERE)/build/obj

LIB_SRC = src/LmaxFix.cpp src/RedactLog.cpp src/FixInitiator.cpp src/MdParse.cpp src/LmaxMdSync.cpp \
          src/BookCheck.cpp src/LmaxMdFeed.cpp src/SimLmaxGateway.cpp
SIM_SRC = $(KSPRPROJ)/sim/src/Mbp2Mbo.cpp $(KSPRPROJ)/sim/src/LevelWindow.cpp
TEST_SRC = tests/test_md_parse.cpp tests/test_md_sync.cpp tests/test_md_feed.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(HERE)/tests -I$(KSPRPROJ)/sim/include -I$(QF_DIR)/include $(INCL) \
        -I$(OPENSSL)/include -I$(GTEST_PATH)/include -DHAVE_SSL=1 -DLMAX_MD_DICT='"$(DICT)"'
QF_LIBS = $(QF_DIR)/libquickfix.a -L$(OPENSSL)/lib -lssl -lcrypto
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) $(QF_LIBS) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

LIB_OBJ = $(patsubst %.cpp,$(OBJDIR)/%.o,$(LIB_SRC)) $(OBJDIR)/sim/Mbp2Mbo.o $(OBJDIR)/sim/LevelWindow.o
TEST_OBJ = $(patsubst %.cpp,$(OBJDIR)/%.o,$(TEST_SRC))

.PHONY: all test dict clean
all: build/run_lmax_md_tests build/lmax_md_check

$(OBJDIR)/%.o: $(HERE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

$(OBJDIR)/sim/%.o: $(KSPRPROJ)/sim/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

build/liblmax_md.a: $(LIB_OBJ)
	ar rcs $@ $^

build/run_lmax_md_tests: $(TEST_OBJ) build/liblmax_md.a
	$(CXX) $(TEST_OBJ) build/liblmax_md.a -o $@ -L$(GTEST_PATH)/lib -lgtest -lgtest_main $(LIBS)

build/lmax_md_check: $(OBJDIR)/tools/lmax_md_check.o build/liblmax_md.a
	$(CXX) $^ -o $@ $(LIBS)

lmax_md_check: build/lmax_md_check

test: build/run_lmax_md_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) ./build/run_lmax_md_tests

# Regenerate the dictionary from LMAX's fix-spec.json and show any difference.
dict:
	$(PYTHON) tools/fixspec2qf.py $(SPEC) --check
	$(PYTHON) tools/fixspec2qf.py $(SPEC) -o build/LMAX-FIX44-MarketData.xml
	diff -u $(DICT) build/LMAX-FIX44-MarketData.xml && echo "dictionary unchanged"

clean:
	rm -rf build

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
