# DeepLOB worked example (synthetic data)

The code behind Section 10.4 ("Training a deep LOB model: a worked example") and
the results table of Section 10.4 of the survey. It trains a PyTorch DeepLOB on a synthetic order book in
which the mid-price is a random walk, so nothing can be predicted, and scores it
against three rules that use no network.

## Files

| File | What it does | Listing in the paper |
|---|---|---|
| `synthetic_book.py` | one day of synthetic snapshots (40 numbers per event) | Listing 1 |
| `lob_normalise.py` | z-score each day with the previous five days; split by time | Listings 2, 4 |
| `lob_windows.py` | labels (smoothed and leak-free), 100-event windows, relative prices | Listing 3 |
| `lob_model.py` | DeepLOB in PyTorch (60,947 parameters) | Listing 5 |
| `lob_train.py` | Adam, cross-entropy, early stopping on validation loss | Listing 6 |
| `lob_evaluate.py` | accuracy and macro-F1 | Listing 7 |
| `run_experiment.py` | builds the data, trains, scores; prints the results | -- |
| `run_output_zscore.txt`, `run_output_relative.txt` | the output reported in the results table of Section 10.4 | -- |

## Requirements

Python 3 with NumPy, PyTorch and scikit-learn. The reported run used
Python 3.14.6, PyTorch 2.14.1, scikit-learn 1.9.1 and NumPy 2.5.3.

```
pip install numpy torch scikit-learn
```

## Running

```
python run_experiment.py            # prices z-scored by day
python run_experiment.py relative   # prices relative to the window's last mid
```

Each command trains two models (smoothed label, then leak-free label) and prints,
for each, the test-day accuracy and macro-F1 of DeepLOB, the past-only rule,
repeating the last known label, and always predicting the most common class.
It uses Apple's GPU (MPS) when available and the CPU otherwise.

## Expected output

The four DeepLOB rows of the results table of Section 10.4:

| Label | Prices | Accuracy | Macro-F1 |
|---|---|---|---|
| smoothed | z-scored | 0.389 | 0.187 |
| smoothed | relative | 0.576 | 0.576 |
| leak-free | z-scored | 0.358 | 0.176 |
| leak-free | relative | 0.358 | 0.176 |

The data and the network's initial weights are seeded. The z-scored
smoothed-label row was also reproduced exactly on CPU; GPU and CPU arithmetic can
differ in the last digits, so other hardware may give slightly different numbers.
