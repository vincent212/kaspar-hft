# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
#   export KSPRPROJ=~/kaspar-hft
#   eval "$(mk_kaspr/detect_paths.sh)"     # must set GTEST_PATH
#   make && ./run_tests

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

# test_aggregator / test_market_maker / test_cvol_formula are deliberately not
# here: Aggregator, MarketMaker and CvolActor do not exist in this repo.
SRC = test_cache_array.cpp \
      test_qcoord.cpp \
      test_pcoord.cpp \
      test_refdata.cpp \
      test_price_units.cpp \
      test_timer.cpp \
      test_som.cpp \
      test_som_cancel_latency.cpp \
      test_ob_delay_queue.cpp \
      test_ob_book.cpp \
      test_slippage_probe.cpp \
      SlippageProbe.cpp \
      test_position.cpp \
      test_position_manager.cpp \
      test_light22.cpp \
      test_light22_order.cpp \
      test_light22_integration.cpp \
      test_latency_probe.cpp \
      test_msg_qlen.cpp

APPNAM = run_tests

# SlippageProbe lives in sim/, which is an application rather than a library,
# so its source is compiled straight into the test binary.
vpath %.cpp $(KSPRPROJ)/sim/src

INCL  += -I$(KSPRPROJ)/unit_test/include -I$(KSPRPROJ)/sim/include -I$(GTEST_PATH)/include
LIBSO += -L$(GTEST_PATH)/lib -lgtest -lgtest_main
LIBSG += -L$(GTEST_PATH)/lib -lgtest -lgtest_main

include $(KSPRPROJ)/mk_kaspr/app_template.mk
