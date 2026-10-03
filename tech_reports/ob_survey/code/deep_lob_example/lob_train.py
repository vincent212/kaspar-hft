import copy
import torch
from torch import nn

def train(model, X_tr, y_tr, X_va, y_va, lr=1e-4, batch=128,
          max_epochs=30, patience=5, device="cpu"):
    model.to(device)
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    # softmax + minus log-probability of the true class
    loss_fn = nn.CrossEntropyLoss()
    best, best_val, bad = None, float("inf"), 0
    for epoch in range(max_epochs):
        model.train()
        # shuffle batches within the training period only
        order = torch.randperm(len(X_tr))
        for i in range(0, len(order), batch):
            j = order[i:i + batch]
            loss = loss_fn(model(X_tr[j].to(device)), y_tr[j].to(device))
            opt.zero_grad(); loss.backward(); opt.step()
        val = validation_loss(model, X_va, y_va, loss_fn, device)
        print(f"epoch {epoch + 1}: validation loss {val:.4f}")
        if val < best_val:
            best, best_val, bad = copy.deepcopy(model.state_dict()), val, 0
        else:
            bad += 1
            if bad >= patience:
                break                    # stop: validation has not improved
    model.load_state_dict(best)          # keep the best weights seen
    return model

@torch.no_grad()
def validation_loss(model, X, y, loss_fn, device, batch=1024):
    model.eval()
    total = 0.0
    for i in range(0, len(X), batch):
        xb, yb = X[i:i + batch].to(device), y[i:i + batch].to(device)
        total += loss_fn(model(xb), yb).item() * len(xb)
    return total / len(X)

@torch.no_grad()
def predict(model, X, device="cpu", batch=1024):
    model.eval()
    out = [model(X[i:i + batch].to(device)).argmax(1).cpu()
           for i in range(0, len(X), batch)]
    return torch.cat(out)
