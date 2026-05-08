# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# define LIBSRC NAM
# LIBSRC is a list of source files
# NAM is the library name (e.g., "chutil" produces libchutil.a and libchutilg.a)

LIBDIRO=$(KSPRPROJ)/lib
LIBDIRG=$(KSPRPROJ)/libg
MAKEDEPEND = $(CC) -M $(MFLAGS) -o $*.d $<

OBJDIRO=obj
OBJO=$(patsubst %.cpp, $(OBJDIRO)/%.o, $(LIBSRC))
OBJDIRG=objg
OBJG=$(patsubst %.cpp, $(OBJDIRG)/%.o, $(LIBSRC))

LIBNAM=lib$(NAM).a
LIBNAMG=lib$(NAM)g.a

.PHONY: opt
opt: $(LIBDIRO)/$(LIBNAM)

.PHONY: debug
debug: $(LIBDIRG)/$(LIBNAMG)

$(LIBDIRO)/$(LIBNAM): $(OBJO)
	@mkdir -p $(LIBDIRO)
	@ar rcs $@ $(OBJO)
	@echo "Built $@"

$(LIBDIRG)/$(LIBNAMG): $(OBJG)
	@mkdir -p $(LIBDIRG)
	@ar rcs $@ $(OBJG)
	@echo "Built $@"

clean:
	@rm -f *.o $(LIBDIRO)/$(LIBNAM) $(LIBDIRG)/$(LIBNAMG)
	@rm -rf obj objg

depend:
	rm *.P

$(OBJDIRG)/%.o: %.cpp | $(OBJDIRG)
	@echo $<
	@$(CXX) $(CFLAGS) $(CFLAGS_DBG) -c $< -o $@

$(OBJDIRG):
	@-mkdir -p $(OBJDIRG)

$(OBJDIRO)/%.o: %.cpp | $(OBJDIRO)
	@echo $<
	@$(CXX) $(CFLAGS) $(CFLAGS_OPT) -c $< -o $@

$(OBJDIRO):
	@-mkdir -p $(OBJDIRO)

%.P : %.cpp
	@$(MAKEDEPEND)
	@sed 's/\($*\)\.o[ :]*/$(OBJDIRG)\/\1.o $(OBJDIRO)\/\1.o $@ : /g' < $*.d > $@; \
		rm -f $*.d; [ -s $@ ] || rm -f $@

-include $(LIBSRC:.cpp=.P)
