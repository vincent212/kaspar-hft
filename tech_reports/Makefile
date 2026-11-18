# Build the papers. Each source lives in tex/<paper>/ with its own figures; the
# PDF is copied to the path the README and external links point to.
#
#   make            all papers
#   make fpga_actors
#   make clean      remove LaTeX build files

PAPERS = fast_send shadow_pov fpga_actors extended_actor_model kaspar_onepager

# Where each PDF is published.
OUT_fast_send            = fast_send.pdf
OUT_shadow_pov           = shadow_pov.pdf
OUT_fpga_actors          = fpga_actors.pdf
OUT_extended_actor_model = extended_actor_model/extended_actor_model.pdf
OUT_kaspar_onepager      = kaspar_onepager.pdf

.PHONY: all clean $(PAPERS)
all: $(PAPERS)

$(PAPERS):
	cd tex/$@ && latexmk -pdf -interaction=nonstopmode -halt-on-error $@.tex
	cp tex/$@/$@.pdf $(OUT_$@)

clean:
	for p in $(PAPERS); do (cd tex/$$p && latexmk -C $$p.tex); done
