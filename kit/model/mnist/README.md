# MNIST weights for the LARGE tinydnn and tinyformer

`train.py` trains both LARGE networks on MNIST and writes the C headers the apps include:
`sw/applications/tinydnn/tinydnn_mnist.h` and `sw/applications/tinyformer/tinyformer_mnist.h`
(int8 weights, int32 biases, one test image and its label).

    python3 -m venv venv && venv/bin/pip install torch torchvision numpy
    venv/bin/python train.py                  # a few minutes on a laptop CPU; MNIST lands in ./data

Training runs the C arithmetic itself (floor shifts, int8 clamps, the integer softmax and layer
norm of `kernels.h`): a soft phase with the rounding relaxed, then quantization-aware fine-tuning.
A numpy emulation of the C code then prints the accuracy the board gets and the RUN_ALL checksum.

After regenerating, set `GOLDEN` in the LARGE branch of each `main.c` to the printed value.
Shapes and `SH_*` shifts live in `DNN` / `TF` at the top of `train.py` and must match the LARGE
branch of `main.c` (the headers `#error` otherwise).

On the board: `MNIST_UART 1` in `config.h`, then `python util/mnist_uart.py --port /dev/ttyUSB1`.
