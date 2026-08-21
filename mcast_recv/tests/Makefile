# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# mcast_recv unit tests (Google Test). The trailer parser is header-only, so
# the tests need only the include path -- no library link.
#   make test   # build and run

CXX ?= g++
GTEST_PATH ?= /usr

INCL = -I../include
CXXFLAGS = -std=gnu++20 -O2 -W -Wall -Wextra $(INCL)

TEST_SRC = $(wildcard test_*.cpp)
TEST_BIN = run_tests

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $@ -L$(GTEST_PATH)/lib -lgtest -lgtest_main -lpthread

clean:
	rm -f $(TEST_BIN)

.PHONY: test clean
