# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Driver for the dbento_pcap_parse tools. Each tool has its own
# Makefile.<name> because app_template.mk builds exactly one executable.
#
#   KSPRPROJ=~/kaspar-hft make          # build all tools (optimized)
#   KSPRPROJ=~/kaspar-hft make debug    # build all tools (debug)
#   KSPRPROJ=~/kaspar-hft make clean

TOOLS = dbento_pcap_to_bin pcap_list_ips build_universe merge_bins verify_merged binstats

.PHONY: all opt debug clean $(TOOLS)

all: opt

opt debug clean:
	@for t in $(TOOLS); do \
	    echo "=== $@ $$t ==="; \
	    $(MAKE) -f Makefile.$$t $@ || exit 1; \
	done

$(TOOLS):
	@$(MAKE) -f Makefile.$@ opt
