# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Nasdaq DROP post-trade client (not certified): generated header, unit tests.
# Not part of the top-level build. Needs the Kaspar libraries (frame, logger,
# mq0, chutil, actors), Boost and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                         # generate, build and run the tests
#   make check-table                  # fieldgen --check on nasdaq_drop.tbl
#   make catalogue                    # the message catalogue as markdown
#
# When the libraries were built in another checkout, point at them:
#   make test KSPRLIB=/path/lib ACTORS_LIB=/path/actors/cpp/libactors.a

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
FIELDGEN   ?= $(KSPRPROJ)/.claude/skills/build-feed-handler/tools/fieldgen.py
PYTHON     ?= python3

TABLE   = $(HERE)/nasdaq_drop.tbl
GEN_HDR = $(HERE)/include/nasdaq_post_trade/drop_msgs.hpp
COMMON  = $(abspath $(HERE)/../common)

LIB_SRC = $(HERE)/src/DropLine.cpp \
          $(HERE)/src/SoupSession.cpp \
          $(HERE)/src/SimDropServer.cpp \
          $(HERE)/src/NasdaqDropClient.cpp \
          $(COMMON)/src/TradeLedger.cpp \
          $(COMMON)/src/EasternTime.cpp
TEST_SRC = $(wildcard $(HERE)/tests/*.cpp)

OBJDIR = $(HERE)/obj
OBJ = $(patsubst $(HERE)/%.cpp,$(OBJDIR)/%.o,$(filter $(HERE)/%,$(LIB_SRC) $(TEST_SRC))) \
      $(patsubst $(COMMON)/src/%.cpp,$(OBJDIR)/common/%.o,$(filter $(COMMON)/%,$(LIB_SRC)))
APP = $(HERE)/run_nasdaq_post_trade_tests

PT_CXXFLAGS = $(STANDARD) -O1 -g -W -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
              -I$(HERE)/include -I$(COMMON)/include $(INCL) -I$(GTEST_PATH)/include
PT_LDLIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
            -L$(BOOST_PATH)/lib -L$(ZMQ_PATH)/lib -L$(GTEST_PATH)/lib \
            -lgtest -lgtest_main -lboost_thread -lboost_filesystem -lzmq -lz -lpthread

.PHONY: all test check-table catalogue clean
all: $(APP)

test: $(APP)
	KSPRPROJ=$(KSPRPROJ) $(APP)

check-table:
	$(PYTHON) $(FIELDGEN) $(TABLE) --check

catalogue:
	@$(PYTHON) $(FIELDGEN) $(TABLE) --markdown

# The header is generated from the field table; never edit it.
$(GEN_HDR): $(TABLE) $(FIELDGEN)
	$(PYTHON) $(FIELDGEN) $(TABLE) --check
	$(PYTHON) $(FIELDGEN) $(TABLE) -o $@

$(APP): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(PT_LDLIBS)

$(OBJDIR)/%.o: $(HERE)/%.cpp $(GEN_HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(PT_CXXFLAGS) -MMD -c $< -o $@

$(OBJDIR)/common/%.o: $(COMMON)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(PT_CXXFLAGS) -MMD -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(APP)

-include $(OBJ:.o=.d)
