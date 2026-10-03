from sklearn.metrics import accuracy_score, f1_score

def score(name, y_true, y_pred):
    acc = accuracy_score(y_true, y_pred)
    f1 = f1_score(y_true, y_pred, average="macro")   # F1 averaged over the three classes
    print(f"{name:<34s} accuracy {acc:.3f}   macro-F1 {f1:.3f}")
    return acc, f1

# Two baselines that use no network:
#   past_rule:   classify the change from the average of the last H mids to the current mid
#   last_label:  repeat the most recent label that is fully known at time t (the label of t - H)
