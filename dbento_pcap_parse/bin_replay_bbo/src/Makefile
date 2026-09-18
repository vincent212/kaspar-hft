# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

# Disable OB's 100ms BBBOChg throttle so the tape records every top-of-book
# change, not just the ~10/sec production callers want. The throttle guard in
# frame_kaspr/src/OB.cpp:notifybbbosubs is wrapped in `#ifndef BBBO_NO_THROTTLE`,
# so this define ONLY takes effect after libfr_kaspr is rebuilt with the same
# define. Build sequence for this tool:
#
#   cd $(KSPRPROJ)/frame_kaspr/src && make clean && \
#       CFLAGS_SPEC="$(CFLAGS_SPEC) -DBBBO_NO_THROTTLE" make
#   cd $(KSPRPROJ)/dbento_pcap_parse/bin_replay_bbo/src && make
#
# Production kaspr must NOT define BBBO_NO_THROTTLE (SOM/light hop-budget rules
# depend on the throttle).
CFLAGS_SPEC += -DBBBO_NO_THROTTLE

SRC = bin_replay_bbo.cpp
APPNAM = bin_replay_bbo

include $(KSPRPROJ)/mk_kaspr/app_template.mk
