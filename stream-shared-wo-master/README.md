# Shared STREAM version

This implementation is geared towards disaggregated memory.
See gem5's composable memory infrastructure on how to get started with the disaggregated memory setup.
This can be found at [https://github.com/darchr/gem5](https://github.com/darchr/gem5), branch `kg/composable-memory-2`

## Prerequisite

This requires a kernel compiled with `CONFIG_UIO` enabled.
On recent kernels, .config would disable it.
On older kernels, .config will set it to `m`.

## Instructions
This version of stream requires n + 1 systems, where n is the number of independent systems working on the same data.
Instance id 0 initializes the stream arrays on a mmapped region in the shared memory space.
It should also verify the results but the verifier has not implemented it.

There are N + 1 systems where N systems work on the shared memory arrays and the extra system is responsible for allocating the arrays.
The programmer needs to make sure that there are no race conditions and the data is coherent.

This version of the program is expected to be slow as it is spinning on a variable for synchronization.