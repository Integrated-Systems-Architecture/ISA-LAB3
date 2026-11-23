#!/usr/bin/env python3
"""Score tinydnn / tinyformer (SIZE LARGE, MNIST_UART 1) on the MNIST test set.

    python mnist_uart.py --port /dev/ttyUSB1 [--baud 9600] [--n 10000]
    python mnist_uart.py --exec path/to/host/app [--n 10000]

Each image goes out as 784 raw bytes (uint8 pixels, row-major); the app
answers with one line `pred=<k> cycles=<c>`. Start the app on the board
first: it waits for the first image. Needs pyserial for --port only.
"""
import argparse
import gzip
import os
import re
import subprocess
import sys
import urllib.request

URL = 'https://ossci-datasets.s3.amazonaws.com/mnist/'
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mnist_data')
NPIX = 28 * 28


def load_test_set():
    data = []
    for name in ('t10k-images-idx3-ubyte.gz', 't10k-labels-idx1-ubyte.gz'):
        path = os.path.join(CACHE, name)
        if not os.path.exists(path):
            os.makedirs(CACHE, exist_ok=True)
            print(f'downloading {URL + name}', file=sys.stderr)
            urllib.request.urlretrieve(URL + name, path + '.part')
            os.replace(path + '.part', path)
        with gzip.open(path) as f:
            data.append(f.read())
    imgs, labels = data
    if imgs[:4] != b'\0\0\x08\x03' or labels[:4] != b'\0\0\x08\x01':
        sys.exit(f'corrupt MNIST files in {CACHE}: delete them and retry')
    return [imgs[16 + NPIX * i:16 + NPIX * (i + 1)] for i in range(10000)], labels[8:]


class HostApp:
    """A host build of the app, talking over its stdin/stdout."""
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def write(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def readline(self):
        return self.p.stdout.readline()

    def close(self):
        self.p.stdin.close()
        self.p.wait()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    link = ap.add_mutually_exclusive_group(required=True)
    link.add_argument('--port', help='serial port of the board, e.g. /dev/ttyUSB1')
    link.add_argument('--exec', metavar='APP', help='host build of the app (stdin/stdout)')
    ap.add_argument('--baud', type=int, default=9600, help='UART_BAUDRATE of the X-HEEP target')
    ap.add_argument('--n', type=int, default=10000, help='how many test images (from the first)')
    ap.add_argument('-v', '--verbose', action='store_true', help='one line per image: index label pred cycles')
    args = ap.parse_args()

    imgs, labels = load_test_set()
    n = min(args.n, len(imgs))
    if args.port:
        import serial
        dev = serial.Serial(args.port, args.baud, timeout=60)
        dev.reset_input_buffer()
    else:
        dev = HostApp(args.exec)

    correct = cycles = 0
    for i in range(n):
        dev.write(imgs[i])
        while True:
            line = dev.readline()
            if not line:
                sys.exit(f'\nno answer to image {i}: is the app running, built with MNIST_UART 1?')
            m = re.search(rb'pred=(\d+)(?: cycles=(\d+))?', line)
            if m:
                break
            sys.stderr.write('app: ' + line.decode(errors='replace'))
        pred, cyc = int(m[1]), int(m[2] or 0)
        correct += pred == labels[i]
        cycles += cyc
        if args.verbose:
            print(i, labels[i], pred, cyc)
        else:
            print(f'\r{i + 1}/{n}: accuracy {100 * correct / (i + 1):.2f}%', end='', flush=True)
    dev.close()
    print(f'\naccuracy {correct}/{n} = {100 * correct / n:.2f}%, mean cycles {cycles / n:.0f}')


if __name__ == '__main__':
    main()
