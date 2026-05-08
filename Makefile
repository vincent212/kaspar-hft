# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

MKFLAGS= -k -w --no-print-directory --quiet

all: install

.PHONY: libdepend
libdepend: TARGET=depend
libdepend: loop

.PHONY: libo
libo: TARGET=opt
libo: loop

.PHONY: libd
libd: TARGET=debug
libd: loop

.PHONY: libc
libc: TARGET=clean
libc: loop

loop: lib1 lib2 lib3 lib4 lib5 lib6 lib7 lib8 lib9 lib10 lib11 lib14

lib1:
	@$(MAKE) -C $(KSPRPROJ)/actors/cpp $(MKFLAGS) $(TARGET)

lib2:
	@$(MAKE) -C $(KSPRPROJ)/chutil/src $(MKFLAGS) $(TARGET)

lib3:
	@$(MAKE) -C $(KSPRPROJ)/mcast_recv/src $(MKFLAGS) $(TARGET)

lib4:
	@$(MAKE) -C $(KSPRPROJ)/mdp3/src $(MKFLAGS) $(TARGET)

lib5:
	@$(MAKE) -C $(KSPRPROJ)/mq0/src $(MKFLAGS) $(TARGET)

lib6:
	@$(MAKE) -C $(KSPRPROJ)/logger/src $(MKFLAGS) $(TARGET)

lib7:
	@$(MAKE) -C $(KSPRPROJ)/frame_kaspr/src $(MKFLAGS) $(TARGET)

lib8:
	@$(MAKE) -C $(KSPRPROJ)/ilink/src $(MKFLAGS) $(TARGET)

lib9:
	@$(MAKE) -C $(KSPRPROJ)/light/src $(MKFLAGS) $(TARGET)

lib10:
	@$(MAKE) -C $(KSPRPROJ)/db/src $(MKFLAGS) $(TARGET)

lib11:
	@$(MAKE) -C $(KSPRPROJ)/mtd/src $(MKFLAGS) $(TARGET)

lib12:
	@$(MAKE) -C $(KSPRPROJ)/super/src $(MKFLAGS) $(TARGET)

lib13:
	@$(MAKE) -C $(KSPRPROJ)/ahedge/src $(MKFLAGS) $(TARGET)

lib14:
	@$(MAKE) -C $(KSPRPROJ)/positionman/src $(MKFLAGS) $(TARGET)


depend: libdepend clean

install: libo

debug: libd

clean: libc
	@find . -name '*.P' -exec rm {} \;
