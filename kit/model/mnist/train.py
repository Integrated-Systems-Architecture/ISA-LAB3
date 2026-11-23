#!/usr/bin/env python3
"""Train the LARGE tinydnn and tinyformer on MNIST and export their C headers.

Both models are trained the way the C code runs them: integer units, int8
weights, int32 biases, power-of-two requant shifts (floor + int8 clamp), and
for tinyformer the shift-based softmax and the integer layer norm of
kernels.h. Training first relaxes the rounding (soft phase), then fine-tunes
with the exact integer forward pass and straight-through gradients (QAT).
emulate_*() re-run the exported integer weights in numpy, mirroring kernels.h
bit for bit: their accuracy is the one the board gets.

    python train.py [--data DIR] [--only tinydnn|tinyformer]
"""
import argparse
import os

import numpy as np
import torch
import torch.nn.functional as F

HERE = os.path.dirname(os.path.abspath(__file__))
APPS = os.path.join(HERE, '..', '..', 'sw', 'applications')

# Shapes and shifts: must match the LARGE branch of each app's main.c (the
# generated header #errors otherwise).
DNN = dict(IMG=28, C1=8, C2=16, FC1=64, NCLASS=10,
           SH_CONV1=7, SH_CONV2=8, SH_FC1=10)
TF = dict(IMG=28, PATCH=7, SEQ=16, DM=64, HEADS=4, DFF=128, NLAYER=1, NCLASS=10,
          SH_PATCH=7, SH_QKV=9, SH_SCORE=7, SH_CTX=8, SH_NORM=0, SH_FF=9,
          SH_RES=5, NORM_GAIN=64)
TEST_IMAGE = 0      # test-set image embedded in the headers for RUN_ALL
SH_LOGITS = 8       # only scales the classifier bias parameter
WSCALE_LN = 32      # int range of the tinyformer weights that feed a layer norm

# --- Integer ops in torch (float64 holds every intermediate exactly) --------

QUANT = False       # False: rounding relaxed (soft phase); True: exact integers


def _ste(x, fn):
    return x + (fn(x) - x).detach() if QUANT else x


def floor(x): return _ste(x, torch.floor)
def trunc(x): return _ste(x, torch.trunc)     # C integer division
def i8(x): return x.clamp(-128, 127)
def requant(acc, sh): return i8(floor(acc / 2 ** sh))   # k_requant_i8


class Net(torch.nn.Module):
    # int8 weight = round(V * scale), V kept in [-1, 127/128] by clip().
    def wparam(self, name, *shape, r=0.5, scale=128):
        setattr(self, name, torch.nn.Parameter(
            torch.empty(shape, dtype=torch.float64).uniform_(-r, r)))
        self.wscale[name] = scale

    # int32 bias = round(V * 2^(sh+4)): sh is the shift after the layer, so
    # V = 1 is 16 output steps.
    def bparam(self, name, n, sh):
        setattr(self, name, torch.nn.Parameter(torch.zeros(n, dtype=torch.float64)))
        self.bshift[name] = sh

    def __init__(self):
        super().__init__()
        self.wscale, self.bshift = {}, {}
        # log2 of the logit scale in the loss; argmax does not see it
        self.temp = torch.nn.Parameter(torch.tensor(-13.0, dtype=torch.float64))

    def ints(self):
        p = {n: _ste(getattr(self, n) * k, torch.round) for n, k in self.wscale.items()}
        p.update({n: _ste(getattr(self, n) * 2 ** (sh + 4), torch.round)
                  for n, sh in self.bshift.items()})
        return p

    def clip(self):
        with torch.no_grad():
            for n in self.wscale:
                getattr(self, n).clamp_(-1, 127 / 128)


class TinyDNN(Net):
    def __init__(self, c=DNN):
        super().__init__()
        self.c = c
        p2 = ((c['IMG'] - 2) // 2 - 2) // 2
        self.wparam('w1', c['C1'], 1, 3, 3)
        self.bparam('b1', c['C1'], c['SH_CONV1'])
        self.wparam('w2', c['C2'], c['C1'], 3, 3)
        self.bparam('b2', c['C2'], c['SH_CONV2'])
        self.wparam('wf1', c['FC1'], c['C2'] * p2 * p2)
        self.bparam('bf1', c['FC1'], c['SH_FC1'])
        self.wparam('wf2', c['NCLASS'], c['FC1'])
        self.bparam('bf2', c['NCLASS'], SH_LOGITS)

    def forward(self, img):
        p, c = self.ints(), self.c
        x = (img >> 1).double()[:, None]
        a = requant(F.max_pool2d(F.conv2d(x, p['w1'], p['b1']).relu(), 2), c['SH_CONV1'])
        a = requant(F.max_pool2d(F.conv2d(a, p['w2'], p['b2']).relu(), 2), c['SH_CONV2'])
        h = requant((a.flatten(1) @ p['wf1'].T + p['bf1']).relu(), c['SH_FC1'])
        return h @ p['wf2'].T + p['bf2']


class TinyFormer(Net):
    def __init__(self, c=TF):
        super().__init__()
        self.c = c
        dm, dff = c['DM'], c['DFF']
        self.wparam('wpatch', dm, c['PATCH'] ** 2)
        self.bparam('bpatch', dm, c['SH_PATCH'])
        self.wparam('pos', c['SEQ'], dm, r=0.1)
        for n in 'qkv':
            self.wparam('w' + n, dm, dm)
            self.bparam('b' + n, dm, c['SH_QKV'])
        # Wo and W2 feed the layer norm, whose uint32 variance wraps once its
        # input RMS passes ~8k: a smaller int range keeps them below that.
        self.wparam('wo', dm, dm, scale=WSCALE_LN)
        self.bparam('bo', dm, c['SH_RES'])
        self.wparam('w1', dff, dm)
        self.bparam('b1', dff, c['SH_FF'])
        self.wparam('w2', dm, dff, scale=WSCALE_LN)
        self.bparam('b2', dm, c['SH_RES'])
        self.wparam('wcls', c['NCLASS'], dm)
        self.bparam('bcls', c['NCLASS'], SH_LOGITS)

    def softmax_q8(self, s):                       # k_softmax_q8
        w = floor(256 * 2 ** -floor((s.max(-1, keepdim=True).values - s) / 4))
        return floor(w * 256 / w.sum(-1, keepdim=True))

    def add_norm(self, acc, res):                  # add_norm + k_layernorm_i32
        c = self.c
        z = acc + res * 2 ** c['SH_RES']
        d = z - trunc(z.sum(-1, keepdim=True) / c['DM'])
        var = floor((d * d).sum(-1, keepdim=True) / c['DM']).clamp(min=1)
        return requant(trunc(d * c['NORM_GAIN'] / floor(var.sqrt())), c['SH_NORM'])

    def forward(self, img):
        p, c = self.ints(), self.c
        n, g, ps, dh = img.shape[0], c['IMG'] // c['PATCH'], c['PATCH'], c['DM'] // c['HEADS']
        x = (img >> 1).double().view(n, g, ps, g, ps).transpose(2, 3).reshape(n, c['SEQ'], ps * ps)
        x = i8(floor((x @ p['wpatch'].T + p['bpatch']) / 2 ** c['SH_PATCH']) + p['pos'])
        q, k, v = (requant(x @ p['w' + t].T + p['b' + t], c['SH_QKV']) for t in 'qkv')
        heads = []
        for h in range(c['HEADS']):
            hs = slice(h * dh, (h + 1) * dh)
            s = floor(q[..., hs] @ k[..., hs].transpose(1, 2) / 2 ** c['SH_SCORE'])
            heads.append(i8(floor(self.softmax_q8(s) @ v[..., hs] / 2 ** c['SH_CTX'])))
        x = self.add_norm(torch.cat(heads, -1) @ p['wo'].T + p['bo'], x)
        f = requant((x @ p['w1'].T + p['b1']).relu(), c['SH_FF'])
        x = self.add_norm(f @ p['w2'].T + p['b2'], x)
        pooled = i8(trunc(x.sum(1) / c['SEQ']))
        return pooled @ p['wcls'].T + p['bcls']


# --- numpy integer emulation of the C code (kernels.h, bit for bit) ---------

def cdiv(a, b):                     # C '/' on ints: truncates toward zero
    q = np.abs(a) // np.abs(b)
    return np.where((a < 0) != (b < 0), -q, q)


def mm(a, b):                       # exact integer matmul through float64
    return np.rint(np.matmul(a.astype(np.float64), b.astype(np.float64))).astype(np.int64)


def np_requant(acc, sh):
    return np.clip(acc >> sh, -128, 127)


def conv_relu(x, w, b):             # k_conv2d_relu_i8, valid 3x3
    win = np.lib.stride_tricks.sliding_window_view(x, (3, 3), axis=(2, 3))
    acc = np.rint(np.einsum('ncyxij,fcij->nfyx', win.astype(np.float64),
                            w.astype(np.float64))).astype(np.int64)
    return np.maximum(acc + b[None, :, None, None], 0)


def maxpool(x):                     # k_maxpool2x2_i32, drops an odd last row/col
    n, c, h, w = x.shape
    x = x[:, :, :h // 2 * 2, :w // 2 * 2].reshape(n, c, h // 2, 2, w // 2, 2)
    return x.max(axis=(3, 5))


def emulate_tinydnn(p, imgs, c=DNN):
    x = (imgs.astype(np.int64) >> 1)[:, None]
    a = np_requant(maxpool(conv_relu(x, p['w1'], p['b1'])), c['SH_CONV1'])
    a = np_requant(maxpool(conv_relu(a, p['w2'], p['b2'])), c['SH_CONV2'])
    h = np_requant(np.maximum(mm(a.reshape(len(a), -1), p['wf1'].T) + p['bf1'], 0), c['SH_FC1'])
    return mm(h, p['wf2'].T) + p['bf2']


def emulate_tinyformer(p, imgs, c=TF):
    n, g, ps, dm = len(imgs), c['IMG'] // c['PATCH'], c['PATCH'], c['DM']
    dh, wrap = dm // c['HEADS'], [0]

    def add_norm(acc, res):
        z = acc + (res << c['SH_RES'])
        d = z - cdiv(z.sum(-1, keepdims=True), dm)
        dd = (d * d) & 0xffffffff                  # (uint32_t)(d * d)
        var = dd.sum(-1, keepdims=True)
        wrap[0] += int((d * d >= 2 ** 31).sum() + (var >= 2 ** 32).sum())
        sd = np.maximum(np.floor(np.sqrt((var & 0xffffffff) // dm)).astype(np.int64), 1)
        return np_requant(cdiv(d * c['NORM_GAIN'], sd), c['SH_NORM'])

    x = (imgs.astype(np.int64) >> 1).reshape(n, g, ps, g, ps).transpose(0, 1, 3, 2, 4)
    x = mm(x.reshape(n, c['SEQ'], ps * ps), p['wpatch'].T) + p['bpatch']
    x = np.clip((x >> c['SH_PATCH']) + p['pos'], -128, 127)
    q, k, v = (np_requant(mm(x, p['w' + t].T) + p['b' + t], c['SH_QKV']) for t in 'qkv')
    ctx = np.zeros_like(x)
    for h in range(c['HEADS']):
        hs = slice(h * dh, (h + 1) * dh)
        s = mm(q[..., hs], k[..., hs].transpose(0, 2, 1)) >> c['SH_SCORE']
        sh = (s.max(-1, keepdims=True) - s) >> 2
        w = np.where(sh < 31, 256 >> np.minimum(sh, 30), 0)
        w = (w * 256) // w.sum(-1, keepdims=True)
        ctx[..., hs] = np.clip(mm(w, v[..., hs]) >> c['SH_CTX'], -128, 127)
    x = add_norm(mm(ctx, p['wo'].T) + p['bo'], x)
    f = np_requant(np.maximum(mm(x, p['w1'].T) + p['b1'], 0), c['SH_FF'])
    x = add_norm(mm(f, p['w2'].T) + p['b2'], x)
    logits = mm(np.clip(cdiv(x.sum(1), c['SEQ']), -128, 127), p['wcls'].T) + p['bcls']
    if wrap[0]:
        print(f'  WARNING: {wrap[0]} int32/uint32 wraps in the layer norm')
    return logits


# --- Training ----------------------------------------------------------------

def accuracy(model, x, y):
    with torch.no_grad():
        return 100.0 * sum((model(x[i:i + 1000]).argmax(1) == y[i:i + 1000]).sum().item()
                           for i in range(0, len(x), 1000)) / len(x)


def train(model, data, epochs, lr, quant, bs=128):
    global QUANT
    QUANT = quant
    xtr, ytr, xte, yte = data[:4]
    opt = torch.optim.Adam(model.parameters(), lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, epochs * -(-len(xtr) // bs))
    for ep in range(epochs):
        model.train()
        perm = torch.randperm(len(xtr))
        for i in range(0, len(xtr), bs):
            idx = perm[i:i + bs]
            shift = tuple(torch.randint(-2, 3, (2,)).tolist())   # +-2 px, MNIST borders are 0
            loss = F.cross_entropy(model(xtr[idx].roll(shift, (1, 2))) * 2 ** model.temp, ytr[idx])
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
            model.clip()
        print(f'  {"qat " if quant else "soft"} epoch {ep + 1}/{epochs}: '
              f'test {accuracy(model, xte, yte):.2f}%', flush=True)


# --- Export ------------------------------------------------------------------

def c_array(decl, a, indent=''):
    v = [str(int(t)) for t in a.ravel()]
    rows = [', '.join(v[i:i + 16]) for i in range(0, len(v), 16)]
    body = (',\n' + indent + '    ').join(rows)
    return f'{indent}{decl} = {{\n{indent}    {body}\n{indent}}}'


def write_header(path, name, c, check, arrays, img, label, acc, golden):
    with open(path, 'w') as f:
        f.write(f'// Generated by model/mnist/train.py: do not edit, rerun it.\n'
                f'// {name} LARGE trained on MNIST. This integer model classifies '
                f'{acc:.2f}% of the\n// 10k test images correctly. RUN_ALL on the '
                f'image below: checksum 0x{golden:08x}.\n\n'
                f'#if {" || ".join(f"{k} != {c[k]}" for k in check)}\n'
                f'#error "{os.path.basename(path)} does not match main.c: '
                f'rerun model/mnist/train.py"\n#endif\n\n')
        for a in arrays:
            f.write(a + ';\n\n')
        f.write(f'// MNIST test image {TEST_IMAGE}: raw uint8 pixels, row-major. '
                f'MNIST_UART reads into it.\n'
                + c_array('static uint8_t mnist_img[IMG * IMG]', img) + ';\n'
                f'#define MNIST_LABEL {label}\n')


def export(name, model, data, emulate, c, check, arrays_of):
    global QUANT
    QUANT = True
    with torch.no_grad():
        p = {k: v.numpy().astype(np.int64) for k, v in model.ints().items()}
    for k, v in p.items():
        lim = 2 ** 31 if k[0] == 'b' else 128
        assert v.min() >= -lim and v.max() < lim, k
    xte, yte = data[2].numpy(), data[3].numpy()
    logits = np.concatenate([emulate(p, xte[i:i + 1000]) for i in range(0, len(xte), 1000)])
    pred = logits.argmax(1)
    acc = 100.0 * (pred == yte).mean()
    torch_acc = accuracy(model, data[2], data[3])
    golden = (int(pred[TEST_IMAGE]) + 1 + int(logits[TEST_IMAGE].sum())) & 0xffffffff
    print(f'{name}: numpy integer emulation {acc:.2f}% (torch QAT {torch_acc:.2f}%), '
          f'test image {TEST_IMAGE}: pred {pred[TEST_IMAGE]} label {yte[TEST_IMAGE]}, '
          f'GOLDEN 0x{golden:08x}')
    path = os.path.join(APPS, name, f'{name}_mnist.h')
    write_header(path, name, c, check, arrays_of(p), xte[TEST_IMAGE], int(yte[TEST_IMAGE]),
                 acc, golden)
    np.savez(os.path.join(data[4], f'{name}_int.npz'), pred=pred, **p)
    print(f'  wrote {os.path.relpath(path)}')


def dnn_arrays(p):
    sizes = dict(w1='C1 * 1 * KS * KS', b1='C1', w2='C2 * C1 * KS * KS', b2='C2',
                 wf1='FC1 * FLAT', bf1='FC1', wf2='NCLASS * FC1', bf2='NCLASS')
    return [c_array(f'static {"int32_t" if k[0] == "b" else "int8_t "} {k}[{s}]', p[k])
            for k, s in sizes.items()]


def tf_arrays(p):
    layer = ',\n'.join(c_array(f'.{k}', p[k], '    ')
                       for k in ('wq', 'wk', 'wv', 'wo', 'w1', 'w2', 'bq', 'bk', 'bv', 'bo', 'b1', 'b2'))
    return [c_array('static int8_t  wpatch[DM * PDIM]', p['wpatch']),
            c_array('static int32_t bpatch[DM]', p['bpatch']),
            c_array('static int8_t  pos[SEQ * DM]', p['pos']),
            'static layer_t layers[NLAYER] = {{\n' + layer + '\n}}',
            c_array('static int8_t  wcls[NCLASS * DM]', p['wcls']),
            c_array('static int32_t bcls[NCLASS]', p['bcls'])]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--data', default=os.path.join(HERE, 'data'), help='MNIST download dir')
    ap.add_argument('--only', choices=['tinydnn', 'tinyformer'])
    ap.add_argument('--epochs', type=int, nargs=2, default=None, metavar=('SOFT', 'QAT'))
    args = ap.parse_args()

    import torchvision
    torch.manual_seed(0)
    tr = torchvision.datasets.MNIST(args.data, train=True, download=True)
    te = torchvision.datasets.MNIST(args.data, train=False, download=True)
    data = (tr.data, tr.targets, te.data, te.targets, args.data)

    if args.only in (None, 'tinydnn'):
        m = TinyDNN()
        soft, qat = args.epochs or (8, 4)
        print('tinydnn')
        train(m, data, soft, 2e-3, False)
        train(m, data, qat, 5e-4, True)
        export('tinydnn', m, data, emulate_tinydnn, DNN,
               ('IMG', 'C1', 'C2', 'FC1', 'NCLASS', 'SH_CONV1', 'SH_CONV2', 'SH_FC1'), dnn_arrays)

    if args.only in (None, 'tinyformer'):
        m = TinyFormer()
        soft, qat = args.epochs or (15, 6)
        print('tinyformer')
        train(m, data, soft, 2e-3, False)
        train(m, data, qat, 5e-4, True)
        export('tinyformer', m, data, emulate_tinyformer, TF,
               ('IMG', 'PATCH', 'SEQ', 'DM', 'HEADS', 'DFF', 'NLAYER', 'NCLASS', 'SH_PATCH',
                'SH_QKV', 'SH_SCORE', 'SH_CTX', 'SH_NORM', 'SH_FF', 'SH_RES', 'NORM_GAIN'),
               tf_arrays)


if __name__ == '__main__':
    main()
