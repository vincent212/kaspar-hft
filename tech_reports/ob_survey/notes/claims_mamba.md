# Claims notes: Mamba in detail (for §10.9 "State-space models", sections/p2_models.tex)

Compiled 2026-10-04. Every claim below comes from a source that was read in full text (PDF via
pdftotext, plus the arXiv LaTeX source for Mamba to check formula notation). Page numbers are the
printed page numbers of the PDF version named. Quotes are verbatim, except that bars over
symbols (lost by pdftotext) were restored from the LaTeX source.

Sources read:
- Gu & Dao, Mamba, arXiv:2312.00752v2 (31 May 2024), full text including Appendices A–E, plus
  arXiv LaTeX source (`src/background.tex`, `src/appendix.tex`, `main.tex` macros, `fig/architecture.pdf`).
- Dao & Gu, Mamba-2 / SSD, arXiv:2405.21060v1 (31 May 2024): §§1–2 in full, §3.1–3.4, §5, Thm 6.1,
  §7.1–7.2, §9.1–9.4 read. The proofs and §8 (systems) were not read line by line.
- Li & Chen, ByteGen, arXiv:2508.02247v2: full text.
- Chen et al., STRATA, arXiv:2608.28060v4: full text.
- MambaStock (arXiv:2402.18959v1) and CryptoMamba (arXiv:2501.01010v2): data and baseline sections
  only, enough to establish the data frequency (daily). Not read in full.

Bib keys: `gu2024mamba` (refs.bib, existing), `li2025bytegen` (bib/refs_tok.bib, existing),
`gao2024mhp`, `popov2026ssm`, `nagy2023` (existing). New keys are in `bib/refs_mamba.bib`:
`dao2024ssd`, `chen2026strata`.

Venue note for `gu2024mamba`: the existing refs.bib entry is `@misc`, year 2023, arXiv only. OpenReview
lists the paper as accepted at COLM 2024 (forum id `tEYskw1VY2`, venue "COLM",
venueid `colmweb.org/COLM/2024/Conference`; checked via api2.openreview.net on 2026-10-04). The arXiv
v2 LaTeX source uses `colm2024_conference.sty` (main.tex line 35). The same OpenReview search also shows
an earlier version listed as an ICLR 2024 rejected submission (forum `AL1fq05o7H`). refs.bib was not
changed; if the survey wants the venue, the entry needs `booktitle = {Conference on Language Modeling (COLM)}`,
`year = {2024}`. No page numbers or DOI were found for the COLM version.

---

## 1. Gu & Dao, "Mamba: Linear-Time Sequence Modeling with Selective State Spaces" — `gu2024mamba`

### 1.1 Continuous-time SSM and discretisation (§2, p. 3)

- The paper's continuous system has **three** matrices, **with no D term**:
  "h'(t) = **A**h(t) + **B**x(t) (1a)   y(t) = **C**h(t) (1b)" (§2, eq. 1, p. 3).
  In this paper the letter D is used for the **number of channels** ("an input sequence x of batch size B
  and length L with D channels", §2, p. 4), not for a feed-through matrix. The ByteGen paper writes a
  four-matrix form with D (its eqs. 7–8, p. 9); the Mamba paper does not. The Mamba paper's text does
  not discuss a skip term D. (The released Mamba code may have one; that code was not checked.)
- Parameters: "S4 models are defined with four parameters (Δ, **A**, **B**, **C**), which define a
  sequence-to-sequence transformation in two stages." (§2, p. 3). The four parameters are Δ, A, B, C.
- The input is one-dimensional: the system "maps a 1-dimensional function or sequence x(t) ∈ ℝ ↦ y(t) ∈ ℝ
  through an implicit latent state h(t) ∈ ℝ^N" (§2, p. 3).
- Discrete recurrence: "h_t = **Ā**h_{t−1} + **B̄**x_t (2a)   y_t = **C**h_t (2b)" (p. 3).
- Convolution form: "**K̄** = (**C B̄**, **C Ā B̄**, …, **C Ā**^k **B̄**, …) (3a)   y = x ∗ **K̄** (3b)" (p. 3).
- Discretisation: "The first stage transforms the 'continuous parameters' (Δ, A, B) to 'discrete
  parameters' (Ā, B̄) through fixed formulas Ā = f_A(Δ, A) and B̄ = f_B(Δ, A, B), where the pair (f_A, f_B)
  is called a discretization rule. Various rules can be used such as the zero-order hold (ZOH) defined in
  equation (4)." (p. 3)
- **ZOH formulas, exact (eq. 4, p. 3; LaTeX source `background.tex` lines 73–75):**

      Ā = exp(ΔA)          B̄ = (ΔA)^{-1} (exp(ΔA) − I) · ΔB          (4)

- Why discretise: it gives "resolution invariance" and "automatically ensuring that the model is properly
  normalized", and "has connections to gating mechanisms of RNNs", but "from a mechanical point of view
  discretization can simply be viewed as the first step of the computation graph in the forward pass of
  an SSM." (p. 3)
- Two ways to compute: "either as a linear recurrence (2) or a global convolution (3)." "Commonly, the
  model uses the convolutional mode (3) for efficient parallelizable training (where the whole input
  sequence is seen ahead of time), and switched into recurrent mode (2) for efficient autoregressive
  inference (where the inputs are seen one timestep at a time)." (p. 3)
- Structure: A is diagonal. "In this case, the A ∈ ℝ^{N×N}, B ∈ ℝ^{N×1}, C ∈ ℝ^{1×N} matrices can all
  be represented by N numbers. … the SSM is applied independently to each channel. Note that in this case,
  the total hidden state has dimension DN per input, and computing it over the sequence length requires
  O(BLDN) time and memory; this is the root of the fundamental efficiency bottleneck" (§2, p. 4).
- Kalman link stated by the authors: SSMs have "inspiration from classical state space models (Kalman
  1960)" (§1, p. 1); "Kalman filters (controls (Kalman 1960))" listed among other meanings of "state space
  model" (§2, p. 4).

### 1.2 LTI (S4) versus selection (S6) (§§2, 3.2, p. 3–6)

- LTI definition: "the model's dynamics are constant through time. In other words (Δ, A, B, C), and
  consequently (Ā, B̄) as well, are fixed for all time-steps. This property is called linear time
  invariance (LTI), which is deeply connected to recurrence and convolutions." (p. 3–4)
- "Thus far, all structured SSMs have been LTI (e.g. computed as convolutions) because of fundamental
  efficiency constraints … a core insight of this work is that LTI models have fundamental limitations in
  modeling certain types of data" (p. 4).
- **Which parameters become input-dependent:** "The main difference is simply making several parameters
  Δ, B, C functions of the input, along with the associated changes to tensor shapes throughout. In
  particular, we highlight that these parameters now have a length dimension L, meaning that the model has
  changed from time-invariant to time-varying." (§3.2, p. 5)
- The selection functions: "We specifically choose s_B(x) = Linear_N(x), s_C(x) = Linear_N(x),
  s_Δ(x) = Broadcast_D(Linear_1(x)), and τ_Δ = softplus, where Linear_d is a parameterized projection to
  dimension d." (§3.2, p. 5)
- **A is NOT input-dependent.** "Interpretation of A. We remark that while the A parameter could also be
  selective, it ultimately affects the model only through its interaction with Δ via Ā = exp(ΔA) (the
  discretization (4)). Thus selectivity in Δ is enough to ensure selectivity in (Ā, B̄), and is the main
  source of improvement. We hypothesize that making A selective in addition to (or instead of) Δ would
  have similar performance, and leave it out for simplicity." (§3.5.2, p. 9). In Algorithm 2, A stays a
  learned parameter of shape (D, N).
- **Algorithms 1 and 2 (p. 6), exact:**

  | step | Algorithm 1 SSM (S4) | Algorithm 2 SSM + Selection (S6) |
  |---|---|---|
  | in/out | x : (B, L, D) → y : (B, L, D) | x : (B, L, D) → y : (B, L, D) |
  | 1 | A : (D, N) ← Parameter | A : (D, N) ← Parameter |
  | 2 | B : (D, N) ← Parameter | B : (B, L, N) ← s_B(x) |
  | 3 | C : (D, N) ← Parameter | C : (B, L, N) ← s_C(x) |
  | 4 | Δ : (D) ← τ_Δ(Parameter) | Δ : (B, L, D) ← τ_Δ(Parameter + s_Δ(x)) |
  | 5 | Ā, B̄ : (D, N) ← discretize(Δ, A, B) | Ā, B̄ : (B, L, D, N) ← discretize(Δ, A, B) |
  | 6 | y ← SSM(Ā, B̄, C)(x) — "Time-invariant: recurrence or convolution" | y ← SSM(Ā, B̄, C)(x) — "Time-varying: recurrence (scan) only" |

  Comment on line 1 of both: "Represents structured N × N matrix". (Here the first B in a shape is batch
  size, distinct from the matrix B.) Note that B and C in Algorithm 2 have no D axis: they are shared
  across the D channels; Δ has one value per channel.
- Name: "we sometimes abbreviate selective SSMs as S6 models, because they are S4 models with a selection
  mechanism and computed with a scan." (Remark 3.1, p. 9)
- Δ initialisation: "the Δ parameter (which can be viewed as a bias term) is initialized to
  τ_Δ^{-1}(Uniform([0.001, 0.1]))" (§3.6, p. 9). A initialisation default for real case is S4D-Real,
  "the n-th element of A as … −(n + 1)" (§3.6, p. 9). Real-valued by default; complex only for the audio
  pretraining experiment (§4.4.1, p. 14).

### 1.3 Motivation: selective copying, induction heads (§3.1, p. 5; §4.1, p. 10–11)

- "We argue that a fundamental problem of sequence modeling is compressing context into a smaller state."
  "attention is both effective and inefficient because it explicitly does not compress context at all.
  … autoregressive inference requires explicitly storing the entire context (i.e. the KV cache), which
  directly causes the slow linear-time inference and quadratic-time training of Transformers. On the other
  hand, recurrent models are efficient because they have a finite state, implying constant-time inference
  and linear-time training. However, their effectiveness is limited by how well this state has compressed
  the context." (§3.1, p. 5)
- Selective Copying "modifies the popular Copying task … by varying the position of the tokens to
  memorize. It requires content-aware reasoning to be able to memorize the relevant tokens (colored) and
  filter out the irrelevant ones (white)." (p. 5)
- Induction Heads "is a well-known mechanism hypothesized to explain the majority of in-context learning
  abilities of LLMs … It requires context-aware reasoning to know when to produce the correct output in the
  appropriate context" (p. 5). Example: "if the model has seen a bigram such as 'Harry Potter' in the
  sequence, then the next time 'Harry' appears in the same sequence, the model should be able to predict
  'Potter' by copying from history." (§4.1.2, p. 10)
- Why LTI fails: "From the recurrent view, their constant dynamics (e.g. the (Ā, B̄) transitions in (2))
  cannot let them select the correct information from their context … the spacing between
  inputs-to-outputs is varying and cannot be modeled by static convolution kernels." (p. 5)
- Principle: "we propose that a fundamental principle for building sequence models is selectivity: or the
  context-aware ability to focus on or filter out inputs into a sequential state." (p. 5)
- Results, Selective Copying (Table 1, p. 11; setting: sequence length 4096, vocab 16, 16 data tokens,
  2-layer models, D = 64, App. E.1 p. 29): accuracy S4 (no gate) 18.3%; S6 (no gate) 97.0%; H3+S4 57.0%;
  Mamba block + S4 56.4%; Mamba (Mamba block + S6) 99.8%.
- Results, Induction Heads (§4.1.2, p. 10; Table 11, App. E): trained at length 2^8 = 256, vocab 16,
  2-layer models; Mamba "generalizes perfectly to million-length sequences, or 4000× longer than it saw
  during training, while no other method goes beyond 2×." Attention models were tested only up to 2^14
  "due to memory limitations" (p. 11).

### 1.4 Interpretation of Δ, B, C — exact wording (§3.5.2, p. 8–9)

- **Δ (exact):** "Interpretation of Δ. In general, Δ controls the balance between how much to focus or
  ignore the current input x_t. It generalizes RNN gates (e.g. g_t in Theorem 1): mechanically, a large Δ
  resets the state h and focuses on the current input x, while a small Δ persists the state and ignores the
  current input. SSMs (1)-(2) can be interpreted as a continuous system discretized by a timestep Δ, and in
  this context the intuition is that large Δ → ∞ represents the system focusing on the current input for
  longer (thus 'selecting' it and forgetting its current state) while a small Δ → 0 represents a transient
  input that is ignored." (p. 9)
- **B and C:** "modifying B and C to be selective allows finer-grained control over whether to let an
  input x_t into the state h_t, or the state into the output y_t. These can be interpreted as allowing the
  model to modulate the recurrent dynamics based on content (input) and context (hidden states)
  respectively." (p. 9)
- Three effects of selection (p. 8–9): **Variable spacing** ("filtering out irrelevant noise tokens …
  for example the presence of language fillers such as 'um'"); **Filtering context** ("selective models
  can simply reset their state at any time to remove extraneous history, and thus their performance in
  principle improves monotonicly with context length"); **Boundary resetting** ("Selective SSMs can also
  reset their state at boundaries (e.g. Δ_t → ∞, or Theorem 1 when g_t → 1)").
- Why Δ is projected to dimension 1 and broadcast: "if a given input x_t should be completely ignored (as
  necessary in the synthetic tasks), all D channels should ignore it, and so we project the input down to
  1 dimension before repeating/broadcasting with Δ." (§3.5.1, p. 8). §3.6 (p. 9) generalises the
  dimension 1 to a small R (low-rank projection); Table 9 uses up to 64.

### 1.5 Connection to gating: Theorem 1 (§3.5.1, p. 8; proof App. C, p. 27–28)

- Context: "the classical gating mechanism of RNNs is an instance of our selection mechanism for SSMs. …
  Theorem 1 is an improvement of Gu, Johnson, Goel, et al. (2021, Lemma 3.1) generalizing to the ZOH
  discretization and input-dependent gates … More broadly, Δ in SSMs can be seen to play a generalized
  role of the RNN gating mechanism. In line with prior work, we adopt the view that discretization of SSMs
  is the principled foundation of heuristic gating mechanisms." (p. 8)
- **Theorem 1 (exact):** "When N = 1, A = −1, B = 1, s_Δ = Linear(x), and τ_Δ = softplus, then the
  selective SSM recurrence (Algorithm 2) takes the form
      g_t = σ(Linear(x_t))
      h_t = (1 − g_t) h_{t−1} + g_t x_t .   (5)" (p. 8)
- Proof sketch as written (App. C, p. 27–28): continuous system is a "leaky integrator"; step size
  "Δ_t = softplus(Linear(x_t))" (the bias Parameter "can be viewed as a learnable bias and folded into the
  linear projection"); then "Ā_t = exp(ΔA) = 1/(1 + exp(Linear(x_t))) = σ(−Linear(x_t)) = 1 − σ(Linear(x_t))"
  and "B̄_t = (ΔA)^{-1}(exp(ΔA) − I)·ΔB = −(exp(ΔA) − I) = 1 − Ā = σ(Linear(x_t))".
  (Typo in the source: the proof writes the continuous system as "h(t) = −h(t) + x(t)" without the
  prime, in both the PDF and `appendix.tex` line 12; it should read h'(t) = −h(t) + x(t), per eq. (1a).)
  Checkable step for students: with A = −1, exp(−softplus(z)) = 1/(1 + e^z) = σ(−z).
- App. A (p. 24): the authors separate RNN gating, which "affects the propagation of signal through time
  and causes inputs to interact along the sequence length dimension", from the looser modern use of
  "gating" for "any multiplicative interaction". They "eschew the term 'gating' in favor of selection".
- §4.1.1 (p. 10): "architecture gating (multiplicative interactions) … does not interact along the
  sequence axis, and cannot affect the spacing between tokens. In particular architecture gating is not an
  instance of a selection mechanism (Appendix A)."

### 1.6 Hardware-aware algorithm (§3.3, p. 6–7; App. D, p. 28–29)

- Why convolution can't be used: selection "loses the equivalence to convolutions (3) with implications
  for its efficiency" (§3.2, p. 5); "The resulting time-varying SSMs cannot use convolutions" (§3 intro,
  p. 5); App. D: "With selectivity, SSMs are no-longer equivalent to convolution, but we leverage the
  parallel associative scan." (p. 28). Algorithm 2 line 6: "Time-varying: recurrence (scan) only" (p. 6).
- Why prior SSMs used convolution: the recurrence "would require computing and materializing the latent
  state h with shape (B, L, D, N), which is much larger (by a factor of N, the SSM state dimension) than
  the input x and output y of shape (B, L, D). Thus the more efficient convolution mode was introduced which
  could bypass the state computation and materializes a convolution kernel (3a) of size only (B, L, D)."
  (§3.3.1, p. 6). Prior LTI models use this "to increase the effective state dimension by a factor of
  N (≈ 10 − 100)" (p. 6).
- Three techniques: "We address this with three classical techniques: kernel fusion, parallel scan, and
  recomputation." (§3.3.2, p. 7)
- FLOPs: "The naive recurrent computation uses O(BLDN) FLOPs while the convolutional computation uses
  O(BLD log(L)) FLOPs, and the former has a lower constant factor. Thus for long sequences and
  not-too-large state dimension N, the recurrent mode can actually use fewer FLOPs." (p. 7)
- Memory-bound: "most operations (except matrix multiplication) are bounded by memory bandwidth … This
  includes our scan operation, and we use kernel fusion to reduce the amount of memory IOs" (p. 7).
- SRAM vs HBM (exact): "instead of preparing the scan input (Ā, B̄) of size (B, L, D, N) in GPU HBM
  (high-bandwidth memory), we load the SSM parameters (Δ, A, B, C) directly from slow HBM to fast SRAM,
  perform the discretization and recurrence in SRAM, and then write the final outputs of size (B, L, D)
  back to HBM." (p. 7)
- Parallel scan: "To avoid the sequential recurrence, we observe that despite not being linear it can
  still be parallelized with a work-efficient parallel scan algorithm (Blelloch 1990; Martin and Cundy
  2018; Smith, Warrington, and Linderman 2023)." (p. 7). [Note: "despite not being linear" is the paper's
  phrase; the recurrence is linear in h but time-varying — the paper does not elaborate.]
- Recomputation: "the intermediate states are not stored but recomputed in the backward pass when the
  inputs are loaded from HBM to SRAM. As a result, the fused selective scan layer has the same memory
  requirements as an optimized transformer implementation with FlashAttention." (p. 7)
- App. D fused kernel steps (p. 28): "1. We read in O(BLD + DN) bytes of memory (Δ, A, B, C) from slow HBM
  to fast SRAM. 2. We discretize to produce Ā, B̄ of size (B, L, D, N) in SRAM. 3. We perform a parallel
  associative scan, yielding intermediate states of size (B, L, D, N) in SRAM. 4. We multiply and sum with
  C, producing outputs of size (B, L, D) and write it to HBM." "This way, we reduce IOs by a factor of O(N)
  (the state dimension), which in practice speeds up the operation by 20-40 times (Section 4.5)."
- Long sequences: "we split the sequences into chunks and perform the fused scan on each chunk" (p. 28).
- Activation memory (App. D, p. 29): "each attention layer (FlashAttention) stores around 12 bytes of
  activations per token, an each MLP layer stores around 20 bytes … Each selective SSM stores around 16
  bytes of activations per token. Hence two layers of selective SSMs have around the same activation
  memory as an attention layer and an MLP layer."

### 1.7 The Mamba block (§3.4, p. 7; Figure 3, p. 8; App. E.2.2)

- Design: "We simplify this architecture by combining these two components [H3 block and MLP block] into
  one, which is stacked homogenously (Figure 3). This is inspired by the gated attention unit (GAU)"
  (p. 7).
- Expansion: "This architecture involves expanding the model dimension D by a controllable expansion
  factor E. For each block, most of the parameters (3ED²) are in the linear projections (2ED² for input
  projections, ED² for output projection) while the inner SSM contributes less." "We always fix to E = 2 in
  our experiments and use two stacks of the block to match the 12D² parameters of a Transformer's
  interleaved MHA … and MLP blocks." (p. 7)
- Activation: "We use the SiLU / Swish activation function … motivated so that the Gated MLP becomes the
  popular 'SwiGLU' variant" (p. 7). Optional LayerNorm "motivated by RetNet's usage" (p. 7).
- Stacking: "We repeat this block, interleaved with standard normalization and residual connections, to
  form the Mamba architecture." (p. 7)
- Figure 3 (p. 8; checked by rendering `fig/architecture.pdf`): input goes to two linear projections
  (two branches). Main branch: linear projection → Conv → σ → SSM. Second branch: linear projection → σ.
  The two branches are multiplied, then a final linear projection. Caption: "Compared to the H3 block,
  Mamba replaces the first multiplicative gate with an activation function. Compared to the MLP block,
  Mamba adds an SSM to the main branch. For σ we use the SiLU / Swish activation".
- App. E.2.2: "the Mamba block is simply the standard SwiGLU block with an extra conv → SSM path added."
- **Not stated in the paper text read:** the Conv's kernel width ("short" / "local" is how H3's conv is
  described: H3 "inserts a standard local convolution, which they frame as a shift-SSM", §2, p. 4). The
  Mamba paper does not give the Mamba conv width in the parts read. Do not state a width (e.g. 4) from
  this paper.
- §3.3 / App. D mention recomputing "output of activation function or short convolution" (p. 28), so
  "short convolution" is the paper's own term.

### 1.8 Complexity and throughput (abstract; §1; §4.5; App. E.5)

- Training linear, inference constant per step (exact, §1, p. 2): "(ii) Fast training and inference:
  computation and memory scales linearly in sequence length during training, and unrolling the model
  autoregressively during inference requires only constant time per step since it does not require a
  cache of previous elements."
- Abstract (p. 1): "Mamba enjoys fast inference (5× higher throughput than Transformers) and linear
  scaling in sequence length".
- §1 (p. 2): "Our Mamba language model has 5× generation throughput compared to Transformers of similar
  size".
- §4.5 (p. 15), the measured statement: "Mamba achieves 4-5× higher inference throughput than a
  Transformer of similar size, since without the KV cache it can use much higher batch sizes. For example,
  a Mamba-6.9B (untrained) would have higher inference throughput than a 5× smaller Transformer-1.3B."
  Figure 8 caption: "as a recurrent model, Mamba can achieve 5× higher throughput than Transformers."
- **Conditions (App. E.5, p. 36):** Mamba 1.4B and untrained Mamba 6.9B vs a "standard Transformer (GPT3
  architecture) at 1.3B and 6.7B size", "standard Transformer implementation in the Huggingface
  transformers library"; "prompt length to be 2048 and the generation length to be 128"; batch size
  varied 1 to 128; throughput = batch size × 128 / time; average of 3 runs; "A100 80GB PCIe GPU".
  So the 5× is a generation-throughput figure that depends on batch size, against the Hugging Face
  Transformer implementation.
- Scan speed (§4.5, p. 15): "Our efficient SSM scan is faster than the best attention implementation that
  we know of (FlashAttention-2 (Dao 2024)) beyond sequence length 2K, and up to 20-40× faster than a
  standard scan implementation in PyTorch." App. D (p. 28): "up to 7× times faster than attention at
  sequence length 32K". §1 (p. 2): "up to 3× faster on A100 GPUs" (vs previous methods). Benchmark
  conditions (App. E.5, p. 35–36): A100 80GB PCIe, batch size 1, D = 1024, N = 16, BF16, core operation
  only ("these do not include the cost of other operations … such as … computing the QKV projections in
  attention").
- Training memory (Table 15, p. 36, 125M models, length 2048): batch 1: Transformer w/ FlashAttention-2
  4.6 GB vs Mamba 4.8 GB; batch 32: 34.5 GB vs 38.2 GB. "Mamba's memory footprint is comparable to the
  most optimized Transformer."

### 1.9 Headline results (with conditions)

- **Language (§4.2, p. 11–12):** Pile dataset, GPT-NeoX tokenizer, 300B tokens for the pretrained
  models; context 2048. Scaling-law runs from ≈125M to ≈1.3B parameters under Chinchilla protocol: "Mamba
  is the first attention-free model to match the performance of a very strong Transformer recipe
  (Transformer++)" (p. 11). Zero-shot (Table 3, p. 12): Mamba-2.8B average accuracy 63.3 vs Pythia-2.8B
  59.1 and Pythia-6.9B 61.7; Abstract: "our Mamba-3B model outperforms Transformers of the same size and
  matches Transformers twice its size". §1: "4 points higher avg. on common sense reasoning compared to
  Pythia-3B and even exceeding Pythia-7B" (p. 2).
- **DNA (§4.3, p. 12–13):** HG38 human genome, ≈4.5B tokens. "at the largest model size of ≈ 40M
  parameters, the curve shows that Mamba can match the Transformer++ and HyenaDNA models with roughly 3× to
  4× fewer parameters" (p. 13, context length 1024). Context-length scaling (≈1.3–1.4M-parameter models,
  lengths 2^10 to 2^20): Mamba's "pretraining perplexity improves as the context increases. On the other
  hand, the HyenaDNA model gets worse with sequence length." (p. 13)
- **Audio (§4.4, p. 14–15):** SC09 speech generation (Table 4): Mamba 6.1M params FID 0.94 vs SaShiMi 5.8M
  FID 1.99 and DiffWave+SaShiMi 23.0M FID 1.42; Mamba 24.3M FID 0.67. §1: "reducing FID on a challenging
  speech generation dataset by more than half" (p. 2). The audio pretraining used the complex
  parameterisation (p. 14).

### 1.10 Ablations relevant to teaching (§4.6, p. 15–17)

- Table 6 (≈350M LM, Pile): Mamba block + S4 (real) perplexity 10.56; Mamba block + S6 8.69. "Replacing any
  of these with a selective SSM (S6) significantly improves performance".
- Table 7: no selective params 10.93; selective Δ only 9.81; selective B only 10.15; selective C only 9.98;
  all three 8.71. "Δ is the most important parameter due to its connection to RNN gating (Theorem 1)"
  (p. 16).
- Table 10 / text: increasing N gives "over a 1.0 perplexity improvement for a cost of only 1% additional
  parameters" (p. 16) — and the table caption says this holds "only when B and C are also selective"
  (N 1→16 with constant B,C: 9.88→9.81; with selective B,C: 9.73→8.71).

### 1.11 Stated limitations (§5 Discussion, p. 16–17)

- **Continuous–discrete tradeoff (exact):** "No Free Lunch: Continuous-Discrete Spectrum. … the selection
  mechanism overcomes their weaknesses on discrete modalities such as text and DNA; but this conversely
  can impede their performance on data that LTI SSMs excel on. Our ablations on audio waveforms examine
  this tradeoff in more detail." (p. 17)
- **Downstream affordances:** whether SSMs have Transformer-like "fine-tuning, adaptation, prompting,
  in-context learning, instruction tuning, RLHF, quantization" properties is open (p. 17).
- **Scale (exact):** "Our empirical evaluation is limited to small model sizes, below the threshold of most
  strong open source LLMs … It remains to assess whether Mamba still compares favorably at these larger
  sizes. We also note that scaling SSMs may involve further engineering challenges and adjustments to the
  model that are not discussed in this paper." (p. 17)
- Related in Mamba-2 (below): the selective scan "does not leverage matrix multiplication units" (Mamba-2
  §2.1, p. 4) — a limitation stated by the same authors in the follow-up.

---

## 2. Dao & Gu, "Transformers are SSMs" (Mamba-2), ICML 2024 — new key `dao2024ssd`

Publication: Proceedings of the 41st ICML, PMLR 235:10041–10071, 2024 (PMLR page, checked 2026-10-04).
arXiv:2405.21060v1, comment "ICML 2024". Page numbers below are arXiv v1.

- **Core claim (abstract, p. 1):** "We show that these families of models are actually quite closely
  related, and develop a rich framework of theoretical connections between SSMs and variants of
  attention, connected through various decompositions of a well-studied class of structured semiseparable
  matrices. Our state space duality (SSD) framework allows us to design a new architecture (Mamba-2) whose
  core layer is an a refinement of Mamba's selective SSM that is 2-8× faster, while continuing to be
  competitive with Transformers on language modeling."
- Title caveat, footnote 1 (p. 1): "Technically speaking, these connections only relate to certain flavors
  of attention; the title of this paper is an homage to Katharopoulos et al. (2020) which first showed that
  'Transformers are RNNs'."
- **SSM = matrix multiplication (§3.1, eq. 3, p. 7):** unrolling the time-varying recurrence gives
  y_t = Σ_{s≤t} C_t^⊤ A_t⋯A_{s+1} B_s x_s, i.e. y = Mx with M_{ji} = C_j^⊤ A_j⋯A_{i+1} B_i (lower
  triangular). Theorem 3.5 (p. 9): "The state space model transformation y = SSM(A, B, C)(x) with state
  size N is identical to matrix multiplication by an N-SS matrix in sequentially semiseparable
  representation". Definition 3.1 (p. 7): "A (lower triangular) matrix M is N-semiseparable if every
  submatrix contained in the lower triangular portion (i.e. on or below the diagonal) has rank at most N."
- **Scalar-times-identity A (§2.4, p. 5):** SSD differs from Mamba's selective SSM in two ways: "The
  structure on A is further simplified from diagonal to scalar times identity structure. Each A_t can also
  be identified with just a scalar in this case." and "We use a larger head dimension P, compared to P = 1
  used in Mamba. Typically P = {64, 128}". "these changes can be viewed as slightly decreasing the
  expressive power in return for significant training efficiency improvements. In particular, our new
  algorithms will allow the use of matrix multiplication units on modern accelerators."
- **Dual (attention-like) form (§2.4, p. 6):** (L ∘ QK^⊤)·V with L_{ij} = a_i × ⋯ × a_{j+1} for i ≥ j and 0
  for i < j, "where a_i are input-dependent scalars bounded in [0, 1]". Differences from softmax attention:
  "The softmax is dropped." and "The attention matrix is multiplied elementwise-wise by an additional mask
  matrix L." L "can be viewed as replacing the heuristic positional embeddings of Transformers with a
  different data-dependent positional mask that controls how much information is transfered across time."
- §5.1 (p. 16): with A = aI, "M_{ji} = A_{j:i}·(C_j^⊤ B_i)", "M = L ∘ (CB^⊤)", and "naively computing the
  scalar structured SSM—by materializing the semiseparable matrix M and performing quadratic matrix-vector
  multiplication—is exactly the same as quadratic masked kernel attention." Correspondence (Fig. 4,
  p. 18): C ↔ Q, B ↔ K, X ↔ V, A_{j:i} ↔ L_{ji}.
- Corollary 5.1 (p. 17): "1-SS SMA (masked attention with 1-semiseparable structured matrices L) (15) is a
  special case of a diagonal SSM (8) where the diagonal matrix is a scalar multiple of the identity."
- Theorem 5.2 (p. 17): "For any instantiation of structured masked attention (Definition 4.2) that is an
  autoregressive process with bounded order, the structured mask L must be a semiseparable matrix."
  (Intro wording, p. 2: "We also prove that any kernel attention method possessing a fast recurrent form
  must be an SSM.")
- Theorem 6.1 (p. 17): for N = P, an algorithm with "O(TN²) training FLOPs, O(TN) inference FLOPs, O(N²)
  inference memory, and whose work is dominated by matrix multiplications."
- **Speed claims:** §1 (p. 2): "A dedicated implementation of SSD is 2 − 8× faster than the optimized
  selective scan implementation of Mamba, while simultaneously allowing for much larger recurrent state
  sizes (8× the size of Mamba or even higher, with minimal slowdown). SSD is highly competitive with
  optimized implementations of softmax attention (FlashAttention-2 (Dao 2024)), crossing over at sequence
  length 2K and 6× faster at sequence length 16K." Figure 10 caption (p. 29): "Our SSD is 2 − 8× faster
  than a Mamba fused scan for large state expansion (N = 64)". §9.3 (p. 31): "it is 2-8× faster than
  Mamba's fused associative scan, which does not leverage matmul units." Caveat stated (p. 31): "the
  Mamba-2 model as a whole might not be as efficient to train as Transformer at short sequence length
  (e.g. at 2K)".
- Mamba-1 limitation as stated here (§2.1, p. 4): the selective SSM "can only be computed in recurrent
  instead of convolutional mode … it is still less efficient than hardware-friendly models such as CNNs and
  Transformers because it does not leverage matrix multiplication units".
- Block change (§7.1, p. 23): Mamba-1 computed A, B, C from the SSM input X after the first projection;
  "In Mamba-2, the SSD layer is viewed as a map from A, X, B, C ↦ Y. It therefore makes sense to produce
  A, X, B, C in parallel with a single projection at the beginning of the block." Plus an extra
  normalisation before the output projection. Proposition 7.2 (p. 24–25): Mamba's S6 has "Head dimension
  P = 1: every channel has independent SSM dynamics A" and B, C "shared across all channels of the input X".
- LM result (§1, p. 3): "Mamba-2 with 2.7B parameters trained on 300B tokens on the Pile outperforms
  Mamba-2.8B, Pythia-2.8B and even Pythia-6.9B trained on the same dataset."

---

## 3. Mamba on limit-order-book / high-frequency financial data

Search performed 2026-10-04: arXiv API (queries: mamba AND "order book" / "limit order" / stock /
"high-frequency" financial / mid-price / cryptocurrency / LOB / FI-2010 / "market microstructure" / tick /
"order flow" / orderbook / "market making" / intraday; "selective state space" AND "order book";
"state space model" AND "order book"; ti:mambastock), OpenAlex (mamba limit order book; mamba order book;
selective state space limit order book; mamba high-frequency trading; mamba stock prediction; LOBMamba),
Crossref (mamba order book). Semantic Scholar returned HTTP 429 on every attempt and was not used. The
web-search tool budget was exhausted for this session, so no general web search was done.

### 3.1 New relative to the survey's SSM table

**(a) ByteGen — `li2025bytegen` (already in bib/refs_tok.bib and cited in the tokenisation section; NOT
yet in §10.9's SSM table).** arXiv preprint 2508.02247v2 (q-fin.CP, 7 Aug 2025), Li & Chen, Stevens
Institute of Technology. Source type: arXiv preprint, no venue found.
- Architecture: an adaptation of H-Net, "a hybrid Mamba-Transformer model that uses a dynamic chunking
  mechanism" (abstract, p. 1); "H-Net … uses Mamba layers for the efficient scanner, Dynamic Chunking for
  the learned segmentation, and Transformer blocks for the high-level reasoner" (§3, p. 5–6); "In H-Net, it
  uses the Mamba-2." (§3.3, p. 10); layout `["m2", ["T6"], "m2"]` (§3.5, p. 11).
- Input: raw bytes, vocabulary 256; each L3 (market-by-order) event packed into 32 bytes (order id + event
  type/flags, exchange timestamp in ns, price float64, quantity float64) (§3.1, p. 7). Training sequences
  3,200–10,240 bytes = 100–320 events (p. 7). Task: next-byte prediction (generative).
- Data: "CME Bitcoin futures (BTCX4) Level 3 (Market-By-Order) data from CME Group via Databento",
  Nov 11–15 2024, "34.2 million orderbook messages" (§4.1, p. 13).
- Models: 8M, 124M, 1.5B parameters (§4.2, p. 14).
- Evaluation: generated vs real statistics only; **no baseline model is compared**. Table 2 (p. 15):
  price volatility 12.6 bps generated vs 16.9 real; fill rate 3.2% vs 8.7%; order lifetime 8.4 s vs
  11.2 s; "Price KL Divergence 0.023". Text: model generates "more cancel orders (47% vs 31%) and fewer
  trades than observed" (p. 15).
- Internal inconsistencies observed (report as-is, do not reconcile): training steps "10,000" (§4.2 and
  Table 4) vs "over 20,000 steps" (§4.4.1); convergence "within 0.5 hour" (§4.2) vs "within 1 hour"
  (§4.4.1); fills are 0.2% of events in Table 1 but the "real" fill rate in Table 2 is 8.7% (metric
  definitions not given); mean inter-event time "13.311ms" (§4.1) vs real "6.941" ms in Table 3.
- Also note: its SSM description writes a D feed-through term and a discretised C̄, D̄ (eqs. 7–10, p. 9);
  this is the ByteGen authors' notation, not the Mamba paper's.

**(b) STRATA — new key `chen2026strata`.** arXiv preprint 2608.28060v4 (cs.CE, 14 Sep 2026), Baidu AI
Cloud / Tsinghua. Source type: arXiv preprint, no venue found. Author list: the arXiv API metadata lists
8 authors including Xiaomin Yuan; the v4 PDF title page lists 7 (no Xiaomin Yuan). Bib entry follows the
arXiv metadata.
- Not an LOB-event model, but its inputs include order-book snapshot fields: "The F = 25 fields per bar
  are open, high, low, close, volume, turnover, trade count, and three levels of ask and bid price, volume
  and order count." (§4.1, p. 8). Five-minute bars, 5 days × 48 bars = 240 steps (§3.1, p. 4).
  **Correction to notes/claims_deep2.md line 173**, which says this paper "does not use LOB data": v4
  uses three-level order-book fields aggregated to 5-minute bars, though not order-book events.
- Model: four "selective state-space blocks": b = (W_u z) ⊙ σ(W_g z), Δ = softplus(W_Δ z + β_ℓ),
  a = clip(e^{−Δ}, 10^{-4}, 0.9999), u_t = a_t ⊙ u_{t−1} + b_t (eqs. 5–7, p. 6); "Both the decay a_t and the
  drive b_t depend on the input at time t, which makes the block selective in the sense of Gu and Dao"
  (p. 6). Computed with a Hillis–Steele associative scan, ⌈log₂T⌉ = 8 passes for T = 240 (p. 7). This is
  a simplified scalar-decay (per-channel, N = 1-like) selective recurrence, not the Mamba block.
- Task: next-day cross-sectional return ranking of ≈1,000 CSI 1000 Chinese A-shares; train 2019–2022,
  validate 2023, test 2024 (§4.1, p. 8).
- Baselines: MLP, LSTM, GRU, TCN, Transformer and "Mamba (S6)", all within ±5% of 244,633 parameters
  (§4.3, p. 9). The "Mamba" baseline is "the selective-scan block implementation of STRATA's own backbone"
  (Table 2, p. 10), not the reference Mamba code.
- Result (Table 3, p. 11, style-residualised, 3 seeds): rank IC STRATA 0.0728 ± 0.0020, GRU 0.0634, Mamba
  (S6) 0.0615, LSTM 0.0604, TCN 0.0593, Transformer 0.0586, MLP 0.0485.
- Stated limitation (§4.7, p. 12–13): the signal's long–short spread is concentrated overnight; over the
  executable session (open to close) STRATA's spread is −1.6 bp/day, t = −0.37: "the decile spread is
  indistinguishable from zero" (abstract). "The results therefore support a prediction-model comparison
  rather than a deployable trading strategy" (§5, p. 14). No transaction costs (§4.8).

### 3.2 Found but outside LOB / high-frequency scope (daily data)

- MambaStock, arXiv 2402.18959v1 (Shi, 2024), preprint; arXiv comment: "substantial text overlap with
  arXiv:2204.02623". Data: four Chinese bank stocks from Tushare (600036.SH, 601288.SH, 601328.SH,
  601988.SH), test set 300 points; plots show 2021–2022 daily closes (§III, p. 3). Baselines include
  ARIMA-NN, XGBoost, LSTM, BiLSTM, Transformer, KF, AttCLX. Daily, not LOB.
- CryptoMamba, arXiv 2501.01010v2, arXiv comment "Published in IEEE International Conference on Blockchain
  and Cryptocurrency (ICBC) 2025" (not checked against IEEE Xplore). "historical daily Bitcoin price data
  from Yahoo Finance covering the period from September 17, 2018, to September 17, 2024" (§4.1); 14-day
  input window. Daily, not LOB.
- SAMBA / Graph-Mamba, arXiv 2410.03707, ICASSP 2025 (DOI 10.1109/ICASSP49660.2025.10888749 per OpenAlex
  and per the STRATA reference list): abstract says "daily stock features". Abstract only read. Daily.
- Many other "Mamba for stock prediction" items appear in OpenAlex (FinMamba, MaGNet, T-Mamba,
  CMDMamba, Attention-Mamba, GHOST, etc.). Titles/abstracts only; none was found that uses LOB data in
  the abstract. Not verified further.

### 3.3 Unverifiable

- **"LOBMamba"**: UNVERIFIED. No paper with this name was found on arXiv, OpenAlex or Crossref (same
  result as notes/claims_dl.md line 264).
- No Mamba-based **mid-price / direction forecaster on LOB data with baselines** (e.g. on FI-2010 or LOBSTER)
  was found in the sources searched. This matches the survey's current statement in §10.9 ("We found no
  peer-reviewed state-space forecaster of the mid-price direction from order-book data with comparisons
  against baselines"). Given Semantic Scholar and web search were unavailable, the search is not exhaustive.

---

## 4. Suggested content for a "Mamba in detail" subsection (plain language, verified facts only)

Each item names its source; formulas are as in the papers.

1. **Start from a continuous system.** Mamba's layer, like S4, starts from a continuous-time system for one
   input channel: h'(t) = A h(t) + B x(t), y(t) = C h(t). The state h has N numbers; A is diagonal, so it
   is also N numbers (Mamba §2, p. 3–4). There is no D term in the paper's equations; the letter D in the
   paper means the number of channels.

2. **Turn it into a step rule with a step size Δ.** Zero-order hold gives Ā = exp(ΔA) and
   B̄ = (ΔA)^{-1}(exp(ΔA) − I)·ΔB, so h_t = Ā h_{t−1} + B̄ x_t, y_t = C h_t (eqs. 2, 4, p. 3). For one
   number with A < 0, Ā = e^{ΔA} lies between 0 and 1. A large Δ makes Ā small (the old state is mostly
   erased); a small Δ makes Ā close to 1 (the old state is kept). This links directly to the survey's
   existing "A = 0.9" example.

3. **S4: the same rule at every step.** If Δ, A, B, C are fixed over time (LTI), the output is a
   convolution of the input with the kernel (CB̄, CĀB̄, CĀ²B̄, …), which can be trained in parallel (eq. 3,
   p. 3). This is the survey's existing "weighted sum of all earlier inputs" paragraph.

4. **Mamba's change: Δ, B and C are computed from the current input; A is not.** B and C are linear
   projections of x_t to N numbers; Δ = softplus(parameter + a projection of x_t) (§3.2, p. 5;
   Algorithm 2, p. 6). A stays a learned constant because it acts only through exp(ΔA), so making Δ
   input-dependent already makes Ā input-dependent (§3.5.2, p. 9). The authors' own reading: "a large Δ
   resets the state h and focuses on the current input x, while a small Δ persists the state and ignores
   the current input" (p. 9). B decides how much of the input enters the state; C how much of the state
   reaches the output (p. 9).

5. **Why this matters: tasks a fixed rule cannot do.** In Selective Copying the tokens to remember sit at
   random positions among filler tokens; a fixed convolution can only count positions, not look at
   content. Accuracy: S4 18.3%, the selective version (S6) 97.0%, full Mamba 99.8% (Table 1, p. 11). On
   Induction Heads, Mamba trained at length 256 stays perfect up to about a million tokens (§4.1.2, p. 10).
   Ablation: making only Δ selective helps most of the three (Table 7, p. 16).

6. **It is a gate in disguise.** With N = 1, A = −1, B = 1 and Δ = softplus(Linear(x_t)), the recurrence
   becomes g_t = σ(Linear(x_t)), h_t = (1 − g_t) h_{t−1} + g_t x_t (Theorem 1, p. 8). This is the update
   form of a gated RNN; the step is exp(−softplus(z)) = σ(−z). This gives students a precise version of
   the survey's "same idea as the forget gate of an LSTM". Unlike an LSTM, the new state is still linear in
   the old state, which is what makes a parallel scan possible (survey's existing argument; Mamba cites
   the parallel scan, §3.3.2, p. 7).

7. **Price of selectivity: no convolution.** Because the weights change from step to step, the convolution
   trick is gone ("Time-varying: recurrence (scan) only", Algorithm 2). Mamba computes the recurrence with
   a parallel scan (§3.3.2, p. 7).

8. **The GPU trick (memory, not arithmetic).** The full set of states has shape (batch, length, channels,
   N), N times larger than the input. Mamba never writes it to the GPU's large, slow main memory (HBM). It
   loads Δ, A, B, C into the small, fast on-chip memory (SRAM), discretises and scans there, and writes back
   only the outputs (kernel fusion). For training, it recomputes the states in the backward pass instead
   of storing them (recomputation). Result: memory like an optimised Transformer with FlashAttention, and
   20–40× faster than a plain PyTorch scan (§3.3.2, p. 7; App. D, p. 28).

9. **The block.** Input → two linear projections (width expanded by E = 2). One branch: short convolution →
   SiLU → selective SSM. Other branch: SiLU. Multiply the two, then a linear projection back down. Blocks
   are stacked with normalisation and residual connections; no attention and no separate MLP block
   (§3.4, p. 7; Fig. 3, p. 8). The paper does not state the convolution width in the text.

10. **Costs.** Training cost grows linearly with sequence length; generation costs a constant amount of time
    and memory per new step, because there is no cache of past inputs (§1, p. 2). Measured on an A100 with
    2048-token prompts and 128 generated tokens, Mamba had 4–5× the generation throughput of a similar-size
    Hugging Face Transformer, mainly because it can run larger batches without a KV cache (§4.5, p. 15;
    App. E.5, p. 36).

11. **Results, briefly.** On language modelling (Pile, 300B tokens), Mamba-2.8B averaged 63.3 on zero-shot
    tasks vs 59.1 for Pythia-2.8B and 61.7 for Pythia-6.9B (Table 3, p. 12). On DNA, perplexity kept
    improving as context grew to about a million base pairs (§4.3.2, p. 13). On SC09 speech generation, a
    6.1M-parameter Mamba had FID 0.94 vs 1.99 for SaShiMi (Table 4, p. 14).

12. **Limits stated by the authors.** Selection helps on discrete data (text, DNA) but "can impede" results
    on continuous signals where fixed-rule SSMs do well; experiments stop at small model sizes (§5, p. 17).
    The follow-up notes the selective scan does not use the GPU's matrix-multiply units (Mamba-2 §2.1, p. 4).

13. **Mamba-2 in one paragraph.** If A is a single number times the identity, the SSM's output equals a
    masked attention without softmax: Y = (L ∘ CB^⊤)X, where the mask entry L_{ij} = a_i⋯a_{j+1} is the
    product of the input-dependent decays between steps j and i (Mamba-2 §2.4, p. 5–6; §5.1, p. 16). C
    plays the role of queries, B of keys, X of values. Computing it in blocks using matrix multiplications
    is 2–8× faster than Mamba's scan (§9.3, p. 31).

14. **Order-book evidence (for the existing Table "State-space models applied to order-book data").**
    Candidates to add, both arXiv preprints:
    - ByteGen~\citep{li2025bytegen}: input = one L3 event as 32 raw bytes, generated byte by byte; model =
      H-Net with Mamba-2 layers around Transformer layers; data = CME Bitcoin futures MBO, 34.2M messages;
      output = next byte; evaluated only against real-data statistics (e.g. volatility 12.6 vs 16.9 bps,
      fill rate 3.2% vs 8.7%), no competing model.
    - STRATA~\citep{chen2026strata}: input = 5-minute bars incl. three levels of bid/ask price, volume and
      order count; model = four simplified selective SSM blocks; output = next-day cross-sectional rank;
      rank IC 0.0728 vs 0.0615 for a Mamba-style baseline and 0.0634 for GRU on Chinese A-shares in 2024;
      over the tradable open-to-close session the long–short spread is indistinguishable from zero.
    Neither is a mid-price/direction forecaster on LOB events with baselines, so the survey's current
    "open direction" conclusion stands.
