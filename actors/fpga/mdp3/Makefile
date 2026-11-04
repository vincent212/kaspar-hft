# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# The MDP3 decoder on the card (phase 1 of ../MDP3_PLAN.md).
#
#   make test          the decoder against the reference on test packets, with g++ (seconds)
#   make hls           Vitis HLS C simulation, synthesis and co-simulation
#                      (needs Vitis: source <Vitis>/settings64.sh)
#   make PACKETS=f.bin test    on another packet file, e.g. recorded packets:
#   test/pcap_to_packets in.pcap f.bin 224.0.31.1 14310
#
# Needs CME's template (genschema/templates_FixBinary.xml) and the C++ codecs
# generated from it (mdp3_sbe/, `make schema` at the top), for the reference.

ROOT ?= $(abspath ../../..)
TEMPLATES ?= $(ROOT)/genschema/templates_FixBinary.xml
VITIS_INCLUDE ?= $(XILINX_HLS)/include
ifeq ($(XILINX_HLS),)
  VITIS_INCLUDE = $(HOME)/tools/Xilinx/2026.1/Vitis/include
endif

WORK ?= work
COUNT ?= 5000
COSIM_COUNT ?= 300
COSIM_PACKETS ?= $(WORK)/cosim.bin
PACKETS ?= $(WORK)/packets.bin
REF = $(PACKETS:.bin=.ref)
HLS_RUN = nice -n 19 taskset -c 1-12

CXXFLAGS = -O2 -Wall -Wextra
TOOLS = test/make_packets test/ref_dump test/pcap_to_packets test/ref_book
DEC = generated/mdp3_messages.hpp include/mdp3fpga/decoder.hpp include/mdp3fpga/events.hpp

.PHONY: test book feed feed_cosim hls hls_csim hls_synth hls_cosim clean
# Capped: the test holds one packet at a time, and this box runs the recorders.
test: $(WORK)/tb_gxx $(REF)
	ulimit -v 4000000; MDP3_PACKETS=$(PACKETS) MDP3_REF=$(REF) $(WORK)/tb_gxx

# Decoder and book against the software book, one instrument. Capped as above.
SECURITY ?= 203686713
BOOKS = $(PACKETS:.bin=.$(SECURITY).book)
book: $(WORK)/tb_book $(BOOKS)
	ulimit -v 4000000; MDP3_PACKETS=$(PACKETS) MDP3_BOOK=$(BOOKS) $(WORK)/tb_book
$(BOOKS): $(REF) test/ref_book
	test/ref_book $(REF) $@ $(SECURITY)
$(WORK)/tb_book: hls/book_top.cpp hls/tb_book_stream.cpp include/mdp3fpga/book.hpp $(DEC)
	mkdir -p $(WORK)
	g++ -std=c++14 -O2 -w -DKFPGA_HLS -DMDP3_SW -DMDP3_SECURITY=$(SECURITY) -I$(VITIS_INCLUDE) -I. -Iinclude -I../include hls/book_top.cpp hls/tb_book_stream.cpp -o $@

# Decoder, filter and batcher against the software book, CONTRACTS listing the
# filter's table: the first is the one compared. Capped as above.
CONTRACTS ?= $(SECURITY)
feed: $(WORK)/tb_feed $(BOOKS)
	ulimit -v 4000000; MDP3_PACKETS=$(PACKETS) MDP3_BOOK=$(BOOKS) $(WORK)/tb_feed
$(WORK)/tb_feed: hls/feed_top.cpp hls/tb_feed_stream.cpp hls/feed_check.hpp include/mdp3fpga/feed.hpp $(DEC) FORCE
	mkdir -p $(WORK)
	g++ -std=c++14 -O2 -w -DKFPGA_HLS -DMDP3_SW -DMDP3_CONTRACTS=$(CONTRACTS) -I$(VITIS_INCLUDE) -I. -Iinclude -I../include hls/feed_top.cpp hls/tb_feed_stream.cpp -o $@
FORCE:

generated/mdp3_messages.hpp: gen_decoder.py $(TEMPLATES)
	mkdir -p generated
	python3 gen_decoder.py $(TEMPLATES) $@

test/make_packets test/ref_dump: %: %.cpp test/dump.hpp
	g++ -std=c++17 $(CXXFLAGS) -I$(ROOT) -Itest $< -o $@
test/ref_book: test/ref_book.cpp
	g++ -std=c++17 $(CXXFLAGS) $< -o $@
test/pcap_to_packets: test/pcap_to_packets.cpp
	g++ -std=c++17 $(CXXFLAGS) $< -o $@

$(WORK)/packets.bin: test/make_packets
	mkdir -p $(WORK)
	test/make_packets $(COUNT) 1 $@
$(WORK)/cosim.bin: test/make_packets
	mkdir -p $(WORK)
	test/make_packets $(COSIM_COUNT) 7 $@
%.ref: %.bin test/ref_dump
	test/ref_dump $< > $@

$(WORK)/tb_gxx: hls/decoder_top.cpp hls/tb_stream.cpp hls/print_event.hpp test/dump.hpp $(DEC)
	mkdir -p $(WORK)
	g++ -std=c++14 -O2 -w -DKFPGA_HLS -DMDP3_SW -I$(VITIS_INCLUDE) -I. -Iinclude -I../include hls/decoder_top.cpp hls/tb_stream.cpp -o $@

# Vitis: C simulation on PACKETS; co-simulation on a smaller set (it simulates
# the Verilog). Kept off cores 0 and 15: this box runs the recorders.
HLS_ENV = MDP3_PACKETS=$(abspath $(PACKETS)) MDP3_REF=$(abspath $(REF))
COSIM_ENV = MDP3_PACKETS=$(abspath $(COSIM_PACKETS)) MDP3_REF=$(abspath $(COSIM_PACKETS:.bin=.ref))
hls: hls_csim hls_synth hls_cosim
hls_csim: $(DEC) $(REF)
	cd hls && $(HLS_ENV) $(HLS_RUN) vitis-run --mode hls --csim --config decoder.cfg --work_dir w > csim.log 2>&1 || { echo "FAILED: hls/csim.log"; exit 1; }
	@grep -hE 'differ|PASS|FAIL' hls/csim.log | sort -u
hls_synth: $(DEC)
	cd hls && $(HLS_RUN) v++ -c --mode hls --config decoder.cfg --work_dir w > synth.log 2>&1 || { echo "FAILED: hls/synth.log"; exit 1; }
	@grep -hE 'Estimated Fmax' hls/synth.log
hls_cosim: hls_synth $(COSIM_PACKETS:.bin=.ref)
	cd hls && $(COSIM_ENV) $(HLS_RUN) vitis-run --mode hls --cosim --config decoder.cfg --work_dir w > cosim.log 2>&1 || { echo "FAILED: hls/cosim.log"; exit 1; }
	@grep -hE 'differ|co-simulation finished' hls/cosim.log | sort -u

# Decoder, filter and batcher as hardware on the XCUX35, on FEED_PACKETS.
FEED_PACKETS ?= $(COSIM_PACKETS)
FEED_ENV = MDP3_PACKETS=$(abspath $(FEED_PACKETS)) MDP3_BOOK=$(abspath $(FEED_PACKETS:.bin=.$(SECURITY).book))
feed_cosim:
	cd hls && $(HLS_RUN) v++ -c --mode hls --config feed_ux35.cfg --work_dir w_feed_ux35 > feed_synth_ux35.log 2>&1 || { echo "FAILED: hls/feed_synth_ux35.log"; exit 1; }
	@grep -hE 'Estimated Fmax|Final II = .*_task' hls/feed_synth_ux35.log | cut -c1-100
	cd hls && $(FEED_ENV) $(HLS_RUN) vitis-run --mode hls --cosim --config feed_ux35.cfg --work_dir w_feed_ux35 > feed_cosim_ux35.log 2>&1 || { echo "FAILED: hls/feed_cosim_ux35.log"; exit 1; }
	@grep -hE 'differ|co-simulation finished' hls/feed_cosim_ux35.log | sort -u

clean:
	rm -rf $(WORK) generated $(TOOLS) hls/w hls/*.log hls/.Xil
