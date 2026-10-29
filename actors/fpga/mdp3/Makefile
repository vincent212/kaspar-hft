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
PACKETS ?= $(WORK)/packets.bin
REF = $(PACKETS:.bin=.ref)
HLS_RUN = nice -n 19 taskset -c 1-12

CXXFLAGS = -O2 -Wall -Wextra
TOOLS = test/make_packets test/ref_dump test/pcap_to_packets
DEC = generated/mdp3_messages.hpp include/mdp3fpga/decoder.hpp include/mdp3fpga/events.hpp

.PHONY: test hls hls_csim hls_synth hls_cosim clean
test: $(WORK)/tb_gxx $(REF)
	MDP3_PACKETS=$(PACKETS) MDP3_REF=$(REF) MDP3_OUT=$(PACKETS:.bin=.card) $(WORK)/tb_gxx

generated/mdp3_messages.hpp: gen_decoder.py $(TEMPLATES)
	mkdir -p generated
	python3 gen_decoder.py $(TEMPLATES) $@

test/make_packets test/ref_dump: %: %.cpp test/dump.hpp
	g++ -std=c++17 $(CXXFLAGS) -I$(ROOT) -Itest $< -o $@
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

$(WORK)/tb_gxx: hls/decoder_top.cpp hls/tb_decoder.cpp test/dump.hpp $(DEC)
	mkdir -p $(WORK)
	g++ -std=c++14 -O2 -I$(VITIS_INCLUDE) -I. -Iinclude -I../include hls/decoder_top.cpp hls/tb_decoder.cpp -o $@

# Vitis: C simulation on PACKETS; co-simulation on a smaller set (it simulates
# the Verilog). Kept off cores 0 and 15: this box runs the recorders.
HLS_ENV = MDP3_PACKETS=$(abspath $(PACKETS)) MDP3_REF=$(abspath $(REF))
COSIM_ENV = MDP3_PACKETS=$(abspath $(WORK)/cosim.bin) MDP3_REF=$(abspath $(WORK)/cosim.ref)
hls: hls_csim hls_synth hls_cosim
hls_csim: $(DEC) $(REF)
	cd hls && $(HLS_ENV) $(HLS_RUN) vitis-run --mode hls --csim --config decoder.cfg --work_dir w > csim.log 2>&1 || { echo "FAILED: hls/csim.log"; exit 1; }
	@grep -hE 'differ|PASS|FAIL' hls/csim.log | sort -u
hls_synth: $(DEC)
	cd hls && $(HLS_RUN) v++ -c --mode hls --config decoder.cfg --work_dir w > synth.log 2>&1 || { echo "FAILED: hls/synth.log"; exit 1; }
	@grep -hE 'Estimated Fmax' hls/synth.log
hls_cosim: hls_synth $(WORK)/cosim.ref
	cd hls && $(COSIM_ENV) $(HLS_RUN) vitis-run --mode hls --cosim --config decoder.cfg --work_dir w > cosim.log 2>&1 || { echo "FAILED: hls/cosim.log"; exit 1; }
	@grep -hE 'differ|co-simulation finished' hls/cosim.log | sort -u

clean:
	rm -rf $(WORK) generated $(TOOLS) hls/w hls/*.log hls/.Xil
