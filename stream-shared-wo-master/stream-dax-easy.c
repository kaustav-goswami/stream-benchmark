// This version of stream uses a shared DAX device to execute STREAM. This
// allows multiple independent systems to run a part of stream.

// The goal of this code is to make it easier to understand and a practice to
// mode pointer code!

// Note that there is no master in this version of STREAM. The workers allocate
// the part of the array and then drops a `m5 exit` to take a checkpoint.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <time.h>

#include <omp.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

// This version of stream uses integers

// Each of the stream array will be allocated this much amount of space. The
// mmap call is just one block of memory os size 3 * STREAM_SIZE
#define STREAM_SIZE 1 << 26
#define BILLION 1000000000L

int main(int argc, char* argv[]) {

    // The shared version of stream needs to have an invoker ID. There has to
    // be a master node that allocates the stream arrays and workers that
    // run a portion of the kernel. A validation node then checks if the
    // results were correctly computed.
    if (argc != 4) {
        printf("usage: # ./stream-shared <node_id> <total_workers> <verify>");
        printf("\n\t verify is 0 or 1\n");
        return -1;
    }

    int node_id = atoi(argv[1]);
    int total_workers = atoi(argv[2]);
    int verify_array = atoi(argv[3]);

    if (node_id >= total_workers) {
        printf("usage: # ./stream-shared <node_id> <total_workers> <verify>");
        printf("\n\t verify is 0 or 1\n");
        return -1;
    }

    if (verify_array < 0 || verify_array > 1) {
        printf("usage: # ./stream-shared <node_id> <total_workers> <verify>");
        printf("\n\t verify is 0 or 1\n");
        return -1;}
    // The start address will changed based on the worker id.

    // The size of the array is fixed. The program needs to be recompiled
    // when changing the array sizes. Make sure that the array of the stream
    // arrays fit into the hugetlbfs page size
    if ((3 * (STREAM_SIZE)) >= (1 << 30) ) {
        printf("error! arrays don't fit into the huge page!\n");
        return -1;
    }

    // print the process id
    printf("Running stream with PID %d\n", getpid());

    // we'll use a dax device to run stream.
    char* dax_mountpoint = "/dev/dax0.0";

    int daxfd = open(dax_mountpoint, O_RDWR);
    if (daxfd < 0) {
        printf("Error mounting! Make sure that the mount point %s is valid\n",
                dax_mountpoint);
        return EXIT_FAILURE;
    }

    // Allocate a 1 GB page. the starting physical address will be same as
    // the dax device. To verify this, print the address.
    int *ptr = (int *) mmap(
                0x0, 1 << 30, PROT_READ | PROT_WRITE, MAP_SHARED, daxfd, 0);

    // Map should not fail if the device is setup correctly.
    if (ptr == MAP_FAILED) {
        perror("Failed to map a 1 GiB page!\n");
        return -1;
    }

    // Mapping worked. Inform the user where the staring address is located.
    printf("Mapping successful at address %#lx\n", (long) ptr);

    // for openmp, if the compiler supports openmp, then then this function
    // call should work. If not (seen in RISCV compilers (old most likely)),
    // then see stream-dax-wo-sync.c on how this part is done.
    int num_threads = omp_get_num_threads(); 

    printf("Number of threads is %ld\n", num_threads);

    // double check to make sure that the number of threads is not 0. It'll
    // crash anyway.
    assert(num_threads != 0);

    // Start allocating data into the mmap file. Only the master node will
    // allocate the arrays.
    // There is no need to have a master in this version of the code.

    // This version can jump into this part here.

    // take the checkpoint here. This also acts as a synchronization point
    // in the implementation. We don't need to have a variable anymore.
    printf("info: worker %d will allocate its part\n", node_id);


    // These are the worker threads. the start address will be shifted by
    // the portion that other workers have already worked on.
    
    // now find the start index and end index for this worker
    int worker_start_index = (node_id) * (STREAM_SIZE)/total_workers;
    int worker_end_index = worker_start_index + (STREAM_SIZE)/total_workers;
    
    // allocate pointers for this version of the code. These pointers start at
    // 0, STREAM_SIZE and 2 x STREAM_SIZE.
    int *A = &ptr[0];
    int *B = &ptr[STREAM_SIZE];
    int *C = &ptr[(STREAM_SIZE) * 2];

    // allocate the memory here.
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        A[i] = 1;
        B[i] = 2;
        C[i] = 0;
    }

    system("m5 exit;");
    
    // After the allocation is complete, we can start the ROI
    printf("this worker %d will start working from %d to %d indices\n",
                        node_id, worker_start_index, worker_end_index);

    // ((uint64_t)(STREAM_SIZE))/(total_workers); 

    // start keeping time
    struct timespec start, stop;
    double time[4];

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
        perror("error! start time for copy kernel failed.");
        return EXIT_FAILURE;
    }
    // skipping the time
    system("m5 resetstats;");
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        C[i] = A[i];
    }
    system("m5 dumpstats;");

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &stop) == -1) {
        perror("error! stop time for copy kernel failed.");
        return EXIT_FAILURE;
    }
    time[0] = (stop.tv_sec - start.tv_sec)
            + (double)(stop.tv_nsec - start.tv_nsec) / (double)BILLION;
    
    ///////////////////////       End of copy kernel      /////////////////////

    int scalar = 3;
    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
        perror("error! start time for scale kernel failed.");
        return EXIT_FAILURE;
    }
    system("m5 resetstats;");
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        B[i] = scalar * C[i];
    }
    system("m5 dumpstats;");

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &stop) == -1) {
        perror("error! stop time for scale kernel failed.");
        return EXIT_FAILURE;
    }
    time[1] = (stop.tv_sec - start.tv_sec)
            + (double)(stop.tv_nsec - start.tv_nsec) / (double)BILLION;
    
    ///////////////////////       End of scale kernel      ////////////////////

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
        perror("error! start time for add kernel failed.");
        return EXIT_FAILURE;
    }
    system("m5 resetstats;");
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        C[i] = A[i] + B[i];
    }
    system("m5 dumpstats;");

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &stop) == -1) {
        perror("error! stop time for add kernel failed.");
        return EXIT_FAILURE;
    }
    time[2] = (stop.tv_sec - start.tv_sec)
            + (double)(stop.tv_nsec - start.tv_nsec) / (double)BILLION;
    
    ///////////////////////       End of add kernel      ////////////////////

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
        perror("error! start time for triad kernel failed.");
        return EXIT_FAILURE;
    }
    system("m5 resetstats;");
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        A[i] = B[i] + scalar * C[i];
    }
    system("m5 dumpstats;");

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &stop) == -1) {
        perror("error! stop time for triad kernel failed.");
        return EXIT_FAILURE;
    }
    time[3] = (stop.tv_sec - start.tv_sec)
            + (double)(stop.tv_nsec - start.tv_nsec) / (double)BILLION;
    
    ///////////////////////       End of triad kernel      ////////////////////

    double data_size_in_bytes = (STREAM_SIZE) * sizeof(int);
    double data_size_in_gib = data_size_in_bytes / 1024.0 / 1024.0 / 1024.0;

    double copy_bw = 2.0 * data_size_in_gib / time[0];
    double scale_bw = 2.0 * data_size_in_gib / time[1];
    double add_bw = 3.0 * data_size_in_gib / time[2];
    double triad_bw = 3.0 * data_size_in_gib / time[3];

    printf(" ================================================\n");
    printf(" array elements     : %d\n", STREAM_SIZE);
    printf(" array SIZE         : %f GiB\n", data_size_in_gib);
    printf("\n");
    printf(" copy bandwidth     : %f GiB/s\n", copy_bw);
    printf(" scale bandwidth    : %f GiB/s\n", scale_bw);
    printf(" add bandwidth      : %f GiB/s\n", add_bw);
    printf(" triad bandwidth    : %f GiB/s\n", triad_bw);
    printf(" ================================================\n");

    // Do we want to verify the arrays? By default, we verify parts of the
    // array independently.
    if (verify_array == 1) {

        // Add a verifier
        //
        // recreate the initialization
        int exp_a = 1, exp_b = 2, exp_c = 0, exp_scalar = 3;
        float avg_a, avg_b, avg_c;
        for (int k = 0; k < 1; k++) {
            exp_c = exp_a;
            exp_b = exp_scalar * exp_c;
            exp_c = exp_a + exp_b;
            exp_a = exp_b + exp_scalar * exp_c;
        }
        int sum_a = 0, sum_b = 0, sum_c = 0;
        for (int i = worker_start_index ; i < worker_end_index ; i++) {
            sum_a += abs(A[i] - exp_a);
            sum_b += abs(B[i] - exp_b);
            sum_c += abs(C[i] - exp_c);
        }
        avg_a = (float)sum_a / (STREAM_SIZE);
        avg_b = (float)sum_b / (STREAM_SIZE);
        avg_c = (float)sum_c / (STREAM_SIZE);

        ///////////////       End of the verifier      ////////////////////
        printf(" ================================================\n");
        printf(" max error A %d, B %d, C %d\n", sum_a, sum_b, sum_c);
        printf(" avg error A %f, B %f, C %f\n", avg_a, avg_b, avg_c);
        printf(" ================================================\n");
    }
    // TODO: close a file descriptor for the huge page
    // ignoring this for now
    
    return 0;
}
