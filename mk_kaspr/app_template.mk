# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# define SRC APPNAM LIBSG LIBSO
# SRC is a list

APPNAMG=$(APPNAM)g
LIBDIRO=$(KSPRPROJ)/lib
LIBDIRG=$(KSPRPROJ)/libg
LIBS_DEPO=$(wildcard $(LIBDIRO)/*.a)
LIBS_DEPG=$(wildcard $(LIBDIRG)/*.a)
MAKEDEPEND = $(CC) -M $(MFLAGS) -o $*.d $<
INSTALLDIRO=/opt/lib
INSTALLDIRG=/opt/libg

OBJDIRO=obj
OBJO=$(patsubst %.cpp, $(OBJDIRO)/%.o, $(SRC))
OBJDIRG=objg
OBJG=$(patsubst %.cpp, $(OBJDIRG)/%.o, $(SRC))

.PHONY: opt
opt: $(APPNAM)

.PHONY: debug
debug: $(APPNAMG)

$(APPNAM): $(OBJO) $(LIBS_DEPO)
	@$(CXX) $(OBJO) -o $@ $(LIBSO) $(LDFLAGS_OPT) -L$(LIBDIRO) -L$(INSTALLDIRO)

$(APPNAMG): $(OBJG) $(LIBS_DEPG)
	@$(CXX) $(OBJG) -o $@ $(LIBSG) $(LDFLAGS_DBG) -L$(LIBDIRG) -L$(INSTALLDIRG)

clean:
	@rm -f *.o $(APPNAM) $(APPNAMG)
	@rm -rf obj objg

depend:
	rm *.P

install: opt
	@cp $(APPNAM) $(KSPRPROJ)/bin

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

-include $(SRC:.cpp=.P)
