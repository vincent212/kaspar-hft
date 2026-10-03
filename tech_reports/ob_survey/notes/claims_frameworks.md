# Claims: deep-learning frameworks used by LOB deep-learning code bases

All checks done **2026-10-03** via the GitHub REST API (`gh api repos/<repo>/contents/...`, `git/trees`),
reading the actual source files / requirements, plus the papers' PDFs where noted.
"VERIFIED" = seen directly in repo files or the publisher page. "UNVERIFIED" = not confirmed.

## Part 1 — LOB projects

| Project | Repo | Framework | Status |
|---|---|---|---|
| DeepLOB (zhang2019) | github.com/zcakhaa/DeepLOB-Deep-Convolutional-Neural-Networks-for-Limit-Order-Books | Keras/TensorFlow (original) **and** PyTorch (added 2021) | VERIFIED |
| LOBCAST (prata2024) | github.com/matteoprata/LOBCAST | PyTorch + PyTorch Lightning | VERIFIED |
| TLOB / MLPLOB (berti2025tlob) | github.com/LeonardoBerti00/TLOB | PyTorch + Lightning | VERIFIED |
| LOBERT (linna2025lobert) | none found | PyTorch (per paper text only) | repo NOT FOUND; framework from paper |
| JAX-LOB (frey2023 / jaxlob2023) | github.com/KangOxford/jax-lob (also KangOxford/AlphaTrade) | JAX (+ Flax, Optax, chex, gymnax, distrax) | framework VERIFIED; "official" status partly UNVERIFIED (see below) |
| LOBFrame (briola2024) | github.com/FinancialComputingUCL/LOBFrame | PyTorch + Lightning | VERIFIED |
| TransLOB (wallbridge2020) | github.com/jwallbridge/translob | Keras / TensorFlow | VERIFIED |

### DeepLOB
- Repo README: "Both tensorflow (version 1 and 2) and pytorch are available."
- Files: `jupyter_tensorflow/run_train_tensorflow-version1.ipynb`, `jupyter_tensorflow/run_train_tensorflow-version2.ipynb`, `jupyter_pytorch/run_train_pytorch.ipynb`.
- TF v1 notebook imports: `import tensorflow as tf`, `import keras`, `from keras.layers import ... CuDNNLSTM`, `from keras.backend.tensorflow_backend import set_session`, `from tensorflow import set_random_seed`.
- TF v2 notebook imports: `import tensorflow as tf`, `import keras`, `from keras.layers import Flatten, Dense, ..., LSTM, Reshape, Conv2D, MaxPooling2D`.
- PyTorch notebook imports: `import torch`, `import torch.nn as nn`, `import torch.optim as optim`, `from torchinfo import summary`.
- History: first release commit `4adde331` (2020-01-23) contained only `jupyter/run_train_represent.ipynb`, which imports `keras` and `tensorflow` (no torch). Commit `bab53a17` (2021-01-12) "change for tensorflow2". PyTorch notebook added in commit "add pytorch version" (2021-07-11). So the original public release was Keras-on-TensorFlow; PyTorch was added ~18 months later.
- The repo describes itself as a demonstration notebook; whether it is the exact code used for the IEEE TSP paper's experiments is UNVERIFIED.

### LOBCAST
- README: "official repository for the paper titled __LOB-Based Deep Learning Models for Stock Price Trend Prediction: A Benchmark Study__" (links Springer AIR, doi 10.1007/s10462-024-10715-4).
- Two branches: default `mini-LOBCAST`, and `v0-LOBCAST` (README: v0 has additional models). Both `requirements.txt` list `torch==1.13.1`, `pytorch_lightning==1.8.6`, `scikit_learn==0.24.2`.
- `src/models/mlp/mlp.py`: `from torch import nn`; `src/models/lobcast_model.py`: `import pytorch_lightning as pl`.

### TLOB / MLPLOB
- README: "This is the official repository for the paper TLOB: A Novel Transformer Model with Dual Attention for Stock Price Trend Prediction with Limit Order Book Data." MLPLOB is in the same repo (`models/mlplob.py`; README: "Training a TLOB, MLPLOB, DeepLOB, or BiNCTABL Model").
- `requirements.txt`: `torch==2.5.0+cu121`, `lightning`, `pytorch_lightning`, `lion_pytorch`, `torch_ema`, `onnx`, `onnxruntime-gpu`.
- `models/tlob.py`, `models/mlplob.py`: `import torch`, `from torch import nn`. `models/engine.py`: `from lightning import LightningModule`.
- Checkpoints in repo are `.pt` files; one `.onnx` export also present (ONNX is an export format, not the training framework).
- The repo also contains its own DeepLOB re-implementation in PyTorch (`models/deeplob.py`).

### LOBERT
- arXiv 2511.12563 (v2), Linna, Baltakys, Iosifidis, Kanniainen, "LOBERT: Generative AI Foundation Model for Limit Order Book Messages" (comment: NeurIPS 2025 GenAI in Finance Workshop submission).
- Paper text (§ Model architecture): "LOBERT's architecture ... builds upon the principles of the original BERT model, implemented using PyTorch." -> framework = PyTorch per the authors' statement. **Not verified against code.**
- Code: the paper PDF (v2) contains no GitHub / code-availability link (grep for "github", "code", "available"). GitHub searches ("LOBERT", "LOBERT limit order book") returned no matching repo; `lobert-23/lobert` exists but is an empty repository with no description (owner unknown, not linked from the paper). The follow-up linna2026impact (arXiv 2609.16930v1) PDF also contains no GitHub link. **Public code: NOT FOUND.**

### JAX-LOB
- arXiv 2308.13289 abstract page and PDF contain **no link to a code repository** (only links to JAX, DeepMind JAX ecosystem, gymnax, LimitOrderBook.jl in references).
- `KangOxford/jax-lob` (owner affiliation on GitHub: "Department of Statistics, Oxford"; co-author Kang Li's contact `kang@robots.ox.ac.uk` is in the README). Description is the paper title; README carries the paper's BibTeX (`frey2023jaxlob`, eprint 2308.13289). `KangOxford/AlphaTrade` has the same description and is more recently updated (pushed 2026-03-16). Treating either as the "official" repo is reasonable but the paper itself does not name one -> **official status UNVERIFIED**.
- Framework evidence (`KangOxford/jax-lob`, default branch `jaxV3`):
  - README dependencies: `pip install jax[cuda]==0.4.11 ... jaxlib==0.4.11 ... distrax brax chex flax optax gymnax wandb`.
  - `gymnax_exchange/jaxob/JaxOrderBookArrays.py`: `import jax`, `from jax import numpy as jnp`, `import chex`.
  - `gymnax_exchange/jaxrl/ppoS5ExecCont.py`: `import jax`, `import flax.linen as nn`, `import optax`, `import gymnax`, `import distrax`, `from flax.training.train_state import TrainState`.
- PyPI package `jaxlob` (v0.19) exists; its metadata gives no homepage/repo URL.
- FLAIROx org has `LOBS5` and `lob_pipeline` repos but no JAX-LOB repo (not inspected further).
- Note: refs.bib has `frey2023` and bib/refs_eval.bib has `jaxlob2023` — both appear to be JAX-LOB (not checked whether both are cited).

### LOBFrame
- README: release accompanying "Deep Limit Order Book Forecasting" (arXiv 2403.09267) and "HLOB" (arXiv 2405.18938).
- `requirements.txt`: `torch==2.0.0+cu118`, `pytorch-lightning==2.0.9`, `lightning==2.0.9`, `torchmetrics==1.2.0`, `torchinfo==1.8.0`.
- `models/DeepLob/deeplob.py`: `import torch`, `import torch.nn as nn`, `import pytorch_lightning as pl`. `optimizers/lightning_batch_gd.py`: `import lightning.pytorch as pl`, `from torchmetrics import Accuracy, F1Score`.

### TransLOB
- README: "This is the repository for the paper _Transformers for Limit Order Books_"; "This is research code and some assembly may be required." Paper PDF `TransLOB.pdf` in repo. Last push 2020-08-09. No requirements file.
- `python/LobAttention.py`: `import tensorflow as tf`, `from keras import backend as K`, `from keras.engine import Layer`.
- `python/LobFeatures.py`: `from keras.layers import ... LSTM, Conv1D, Conv2D ...`, `from keras.models import Model`.
- `python/LobTransformer.py`: `from tensorflow.keras.layers import InputSpec`, `from keras import initializers, constraints, activations`. (Also imports `from tLobAttention import MultiHeadSelfAttention`, a module name not present in the repo — consistent with the "assembly may be required" note.)

### Summary for the survey text
- PyTorch: LOBCAST, TLOB/MLPLOB, LOBFrame (all with Lightning); DeepLOB (added 2021); LOBERT (per paper, no public code found).
- Keras/TensorFlow: DeepLOB (original 2020 release), TransLOB.
- JAX: JAX-LOB.

## Part 2 — Framework citations (bib/refs_frameworks.bib)

Duplicate check: grep of `refs.bib` and `bib/*.bib` for `paszke|abadi|bradbury|chollet|pedregosa|jax2018` keys returned nothing; all five keys are new.

Note: `main.tex` line 116 `\bibliography{...}` does not include `bib/refs_frameworks`. It must be added there for the keys to resolve.

- **paszke2019pytorch** — VERIFIED against NeurIPS proceedings page and its official BibTeX: 21 authors (Paszke ... Chintala), "PyTorch: An Imperative Style, High-Performance Deep Learning Library", Advances in Neural Information Processing Systems 32, Curran Associates, 2019; editors Wallach, Larochelle, Beygelzimer, d'Alché-Buc, Fox, Garnett. arXiv 1912.01703 (author list matches). **Pages: the official NeurIPS BibTeX has `pages = {}`; the commonly quoted "8024–8035" is UNVERIFIED and was omitted.** No DOI on the NeurIPS page.
- **abadi2016tensorflow** — VERIFIED against USENIX page metadata: 22 authors (Abadi ... Zheng), "TensorFlow: A System for Large-Scale Machine Learning", 12th USENIX Symposium on Operating Systems Design and Implementation (OSDI 16), pp. 265–283, Savannah, GA, ISBN 978-1-931971-33-1, 2016. No DOI.
- **bradbury2018jax** — VERIFIED against the "Citing JAX" section of github.com/jax-ml/jax README: `@software{jax2018github, ...}`, 12 authors in alphabetical order (Bradbury ... Zhang), title "{JAX}: composable transformations of {P}ython+{N}um{P}y programs", url http://github.com/jax-ml/jax, version 0.3.13, year 2018. README says year = open-source release and version should match `jax/version.py`. Converted to `@misc` (plainnat does not know `@software`).
- **chollet2015keras** — VERIFIED against keras.io FAQ BibTeX (`@misc{chollet2015keras, title={Keras}, author={Chollet, Fran\c{c}ois and others}, year={2015}, howpublished={\url{https://keras.io}}}`) and keras-team/keras `CITATION.cff` (Chollet, François + "others"/"Keras Contributors", date-released 2015-03-27, url https://keras.io).
- **pedregosa2011sklearn** — VERIFIED against jmlr.org page and its BibTeX: 16 authors (Pedregosa ... Duchesnay), "Scikit-learn: Machine Learning in Python", JMLR 12(85):2825–2830, 2011, ISSN 1533-7928. No DOI.
