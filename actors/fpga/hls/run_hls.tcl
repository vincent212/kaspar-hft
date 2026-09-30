# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Synthesize each runtime part on its own and print its latency in cycles.
#   cd actors/fpga/hls && vitis_hls -f run_hls.tcl
#
# PART  FPGA part. Default: the Virtex UltraScale+ VU2P of the AMD Alveo UL3524.
#       CHECK the exact part string (package, speed grade) against the card's
#       documentation before quoting numbers.
# CLOCK_NS  clock period in ns. Default 3.2 (312.5 MHz).

set part  [expr {[info exists ::env(PART)]     ? $::env(PART)     : "xcvu2p-fsvj2104-3-e"}]
set clock [expr {[info exists ::env(CLOCK_NS)] ? $::env(CLOCK_NS) : "3.2"}]

set tops {bench_actor_step bench_host_in bench_host_out bench_fast_send}
set flags "-std=c++14 -DKASPAR_VITIS -I../include -I../examples/ping_pong"

foreach top $tops {
  open_project -reset kfpga_hls/$top
  set_top $top
  add_files bench_tops.cpp -cflags $flags
  open_solution -reset solution1 -flow_target vivado
  set_part $part
  create_clock -period $clock -name default
  csynth_design
  close_project
}

# The full design: does the free-running DATAFLOW form synthesize?
open_project -reset kfpga_hls/pingpong_top
set_top pingpong_top
add_files pingpong_top.cpp -cflags $flags
open_solution -reset solution1 -flow_target vivado
set_part $part
create_clock -period $clock -name default
csynth_design
close_project

puts "Latency reports: kfpga_hls/<top>/solution1/syn/report/<top>_csynth.rpt"
exit
