
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

LIBSRC = light22.cpp QCoord.cpp PCoord.cpp

# Enable AVX2 for point::fast_compare (x86 only). Still required after
# TachBook.cpp was dropped: light22.cpp pulls point.hpp too.
ifneq ($(UNAME_S),Darwin)
DEFINES_COMMON += -mavx2
endif

NAM = light

include $(KSPRPROJ)/mk_kaspr/lib_template.mk
