# Block 6: the inference of the network

Part of the 8.1 plan in [`DEVELOPMENT_8.1.md`](../../../DEVELOPMENT_8.1.md). This block covers the code that runs
the Consilium network in the engine: the accumulators, the forward pass through the layers after the accumulator,
the SIMD kernels, and the loading of the network file.

## 6.1 Starting point

For Consilium the part of the inference that runs at every node has already been largely rewritten (accumulators,
the four experts, our input blocks, the link with the search). The forward pass of the layers, its SIMD kernels and
the loading of the network still come from the inference code the project started from, about 60 to 70% of the lines
of `source/nnue/` by a line-by-line count ([`source/nnue/README.md`](../../../source/nnue/README.md)).

## 6.2 Method

This block has a property the search blocks do not have: the result of the inference is fixed by the network file.
The rewritten code must give **bit-identical evaluations**, which is checked directly:

1. every evaluation compared with the old code on several million positions from games, in a verification build
   that stops at the first difference;
2. the bench and the search tree unchanged on every build variant (SSE4.1, AVX2, AVX-512, VNNI);
3. speed measured with hardware counters at the identical tree.

Since the evaluations are the same, strength is unchanged by construction, and no SPRT is needed; the only open
question is speed, and a slower kernel is not adopted.

Status: planned. It does not depend on the search blocks and can proceed in parallel with them.
