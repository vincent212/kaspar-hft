# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Clear Street FIX 4.2 order manager (not certified; never connected): the
# dictionary, the library and the unit tests. Not part of the top-level
# build. Needs the kaspar libraries (frame, logger, mq0, chutil, actors),
# Boost, ZeroMQ, OpenSSL, Google Test and a static QuickFIX built with
# HAVE_SSL (QF_DIR/include/quickfix/*.h, QF_DIR/libquickfix.a) and QuickFIX's
# FIX42.xml (QF_SPEC):
#
#   export KSPRPROJ=~/kaspar-hft
#   make test QF_DIR=/path/to/qfbuild QF_SPEC=/path/to/quickfix/spec/FIX42.xml
#   make dict QF_SPEC=...          # build/ClearStreet-FIX42.xml only
#
# Everything generated goes to build/ (git-ignored).

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
QF_DIR     ?= $(HOME)/local/quickfix
QF_SPEC    ?= $(QF_DIR)/spec/FIX42.xml
OPENSSL    ?= /opt/homebrew/opt/openssl@3
CXX        ?= c++
PYTHON     ?= python3

DICT   = $(HERE)/build/ClearStreet-FIX42.xml
OBJDIR = $(HERE)/build/obj

LIB_SRC  = src/CsFix.cpp src/QfCsSession.cpp src/ClearStreetOM.cpp src/ClearStreetOMConfig.cpp src/SimClearStreet.cpp
TEST_SRC = tests/test_cs_fix.cpp tests/test_clearstreet_om.cpp tests/test_sim_clearstreet.cpp tests/test_qf_session.cpp
BOOK     = $(KSPRPROJ)/frame_kaspr/src/BrokerOrderBook.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(HERE)/tests -I$(QF_DIR)/include $(INCL) \
        -I$(OPENSSL)/include -I$(GTEST_PATH)/include -DHAVE_SSL=1 -DCS_DICT='"$(DICT)"' \
        -DCS_HERE='"$(HERE)"'
QF_LIBS = $(QF_DIR)/libquickfix.a -L$(OPENSSL)/lib -lssl -lcrypto
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) $(QF_LIBS) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

LIB_OBJ  = $(patsubst %.cpp,$(OBJDIR)/%.o,$(LIB_SRC)) $(OBJDIR)/BrokerOrderBook.o
TEST_OBJ = $(patsubst %.cpp,$(OBJDIR)/%.o,$(TEST_SRC))

.PHONY: all dict test clean
all: build/run_clearstreet_om_tests

dict: $(DICT)

$(DICT): $(HERE)/tools/mkdict.py $(QF_SPEC)
	@mkdir -p $(dir $@)
	$(PYTHON) $(HERE)/tools/mkdict.py $(QF_SPEC) $@

$(OBJDIR)/%.o: $(HERE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

$(OBJDIR)/BrokerOrderBook.o: $(BOOK)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

build/libclearstreet_om.a: $(LIB_OBJ)
	ar rcs $@ $^

build/run_clearstreet_om_tests: $(TEST_OBJ) build/libclearstreet_om.a $(DICT)
	$(CXX) $(TEST_OBJ) build/libclearstreet_om.a -o $@ -L$(GTEST_PATH)/lib -lgtest -lgtest_main $(LIBS)

test: build/run_clearstreet_om_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) ./build/run_clearstreet_om_tests

clean:
	rm -rf build

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
