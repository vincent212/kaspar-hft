# BUG: `shadow_pov.tex` — issues to resolve before arXiv submission

| | |
|---|---|
| **Severity** | Low–Medium — no code impact; blocks a clean arXiv submission of the paper |
| **Component** | `tech_reports` (Shadow-PPOV paper) |
| **Files** | `tech_reports/shadow_pov.tex` (+ `tech_reports/sim/*.pdf` figures) |
| **Status** | Open |
| **Date** | 2026-09-15 |

## Summary

Proofread of `shadow_pov.tex` for arXiv (`q-fin.TR`). The prose is clean — no
typos or grammar errors, all cross-references and citations resolve, the
arithmetic checks out (e.g. `0.0955 ticks × $12.50 = $1.19`), and the DRAFT
watermark block has been removed. The items below are what remains before the
paper is submission-ready. None affect the running system.

The bibliography is inline (`\begin{thebibliography}`), so there is no `.bbl`/`.bib`
to manage; the arXiv upload is `shadow_pov.tex` **plus the `sim/` folder** (6
figure PDFs, all present).

## Should fix

### 1. Three bibliography entries have no author

arXiv referees flag author-less references. Look up the authors from the arXiv
abstract pages and fill them in (do **not** invent them):

- `bibitem{bayesiantca2019}` (line ~1503) — arXiv:1904.01566
- `bibitem{scheduletactics2014}` (line ~1526) — arXiv:1409.1441
- `bibitem{limitmarkettactics2014}` (line ~1528) — arXiv:1409.1442
- `bibitem{cesari2012}` (line ~1530) — currently only `R.~Cesari et al.`; expand
  to the full author list (arXiv:1206.5324).

### 2. Verify the arXiv id for `passiveimpact2026`

`bibitem{passiveimpact2026}` cites **arXiv:2607.28323** (line ~1544). The number
28323 for July 2026 looks unusually high; confirm the id is correct and the link
resolves, or correct it.

## Should check (data / disclosure — author decision, not a text edit)

### 3. Mark-out table uses a different (smaller) passive sample than the main table

`tab:markout` caption states "Passive figures are 20 sessions and aggressive
246," whereas `tab:styles` reports passive over 246 sessions (9,592 windows). The
passive relative slippage consequently differs between the two tables
(`+0.072 ± 0.044` in `tab:markout` vs `+0.0955 ± 0.0130` in `tab:styles`). It is
disclosed, but a reviewer will likely ask why the passive mark-out sample is only
20 sessions. Either re-run the passive mark-outs over the full 246 sessions to
match, or add a sentence explaining the smaller sample.

## Optional / cosmetic (won't block submission)

### 4. Inconsistent arXiv link formatting in the bibliography

Some entries hyperlink (`\href{https://arxiv.org/abs/...}{arXiv:...}`:
`gueant2012`, `contkukanov2015`, `laruelle2011`) while others print plain
`arXiv:...` text (`negdrift2024`, `scheduletactics2014`, `limitmarkettactics2014`,
`passiveimpact2026`, `queuereactive2025`, `rlexec2025`, `mmdilemma2025`,
`bayesiantca2019`, `cesari2012`). Normalize to one style.

### 5. Harmless font warnings

`pdflatex` warns `T1/lmr/bx/sc undefined` and `T1/lmr/m/scit undefined` (bold
small-caps / small-caps-italic in the title and `\textsc` uses). The title still
renders; substitution is silent. Optional: load a font family with those shapes
(e.g. a small-caps-capable package) if pixel-perfect small caps are wanted.

### 6. `\bibitem` keys vs printed years (no action needed)

`almgren2000` prints "2001" and `contkukanov2015` prints "2017". These are label
names only; nothing shows in the text. Left as-is deliberately.

## Verification

`pdflatex shadow_pov.tex` (×2) compiles to a 25-page PDF with no undefined
references or citations; only the cosmetic font warnings of item 5 appear.
