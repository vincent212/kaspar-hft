
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

LIBSRC = light22.cpp QCoord.cpp PCoord.cpp TachBook.cpp

# Enable AVX2 for TachBook fast_compare function (x86 only)
ifneq ($(UNAME_S),Darwin)
DEFINES_COMMON += -mavx2
endif

NAM = light

include $(KSPRPROJ)/mk_kaspr/lib_template.mk
