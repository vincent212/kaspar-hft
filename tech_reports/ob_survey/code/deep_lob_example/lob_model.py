import torch
from torch import nn

def conv(c_in, c_out, k, s=(1, 1), pad="same"):
    if s != (1, 1):
        pad = 0                          # strided convolutions shrink the columns
    return nn.Sequential(nn.Conv2d(c_in, c_out, k, stride=s, padding=pad), nn.LeakyReLU(0.01))

def time_convs(c):                       # two 4 x 1 convolutions over consecutive events
    return nn.Sequential(conv(c, c, (4, 1)), conv(c, c, (4, 1)))

class DeepLOB(nn.Module):
    def __init__(self):
        super().__init__()
        self.stage1 = nn.Sequential(conv(1, 16, (1, 2), (1, 2)), time_convs(16))   # price with size
        self.stage2 = nn.Sequential(conv(16, 16, (1, 2), (1, 2)), time_convs(16))  # ask with bid
        self.stage3 = nn.Sequential(conv(16, 16, (1, 10), pad=0), time_convs(16)) # across the book
        self.inc_a = nn.Sequential(conv(16, 32, (1, 1)), conv(32, 32, (3, 1)))
        self.inc_b = nn.Sequential(conv(16, 32, (1, 1)), conv(32, 32, (5, 1)))
        self.inc_c = nn.Sequential(nn.MaxPool2d((3, 1), stride=1, padding=(1, 0)), conv(16, 32, (1, 1)))
        self.lstm = nn.LSTM(input_size=96, hidden_size=64, batch_first=True)
        self.out = nn.Linear(64, 3)

    def forward(self, x):                        # x: batch x 100 x 40
        h = x.unsqueeze(1)                       # batch x 1 x 100 x 40
        h = self.stage3(self.stage2(self.stage1(h)))          # batch x 16 x 100 x 1
        h = torch.cat([self.inc_a(h), self.inc_b(h), self.inc_c(h)], dim=1)  # batch x 96 x 100 x 1
        h = h.squeeze(3).transpose(1, 2)         # batch x 100 x 96
        _, (h_last, _) = self.lstm(h)            # memory after the last event
        return self.out(h_last[-1])              # batch x 3 scores (softmax is in the loss)
