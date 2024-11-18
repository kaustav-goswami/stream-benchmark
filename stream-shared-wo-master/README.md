# Shared STREAM version

This implementation is geared towards disaggregated memory.
See gem5's composable memory infrastructure on how to get started with the disaggregated memory setup.
This can be found at [https://github.com/darchr/gem5](https://github.com/darchr/gem5), branch `kg/composable-memory-2`

In this version, workers allocate their own part of the array before starting
STREAM.
There is no notion of a master node.

## Prerequisite

This requires a kernel compiled with `CONFIG_UIO` enabled.
On recent kernels, .config would disable it.
On older kernels, .config will set it to `m`.

For the DAX version, make sure all kconfig params for DAX, SPARSE\_MEM, PMEM
etc. are set.

## Instructions
This version of stream requires n systems, where n is the number of independent systems working on the same data.
It should also verify the results but the verifier has not implemented it.

The programmer needs to make sure that there are no race conditions and the data is coherent.

This version of the program is expected to be slow as it is spinning on a variable for synchronization at the end of all the arrays?

## Versions

* stream-addr.c Uses UIO and m5 with ARM addresses
* stream-dax-wo-sync.c Uses DAX and m5 without addresses and no O\_SYNC.
