// This version of stream uses a shared DAX device to execute STREAM. This
// allows multiple independent systems to run a part of stream.
//
// The dax region can be either DRAM or NVM. This is configurable as an
// argument paramaeter

// Usage:
// $ gcc -fopenmp -o stream     # Try g++ if this fails
// $ ./stream <0/1>           # 0 -> mapped at dax0.0
//                              # 1 -> mapped at dax1.0

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <time.h>

// #include <omp.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>


// There are three MMAP calls for each of the arrays!
// This version of stream uses double

// Each of the stream array will be allocated this much amount of space. The
// mmap call is just one block of memory os size 3 * STREAM_SIZE
#define STREAM_SIZE 1 << 30
#define BILLION 1000000000L

int main(int argc, char* argv[]) {

    // The shared version of stream needs to have an invoker ID. There has to
    // be a master node that allocates the stream arrays and workers that
    // run a portion of the kernel. A validation node then checks if the
    // results were correctly computed.

    int node_id = 0;
    int verify_array = 1;

    if (argc != 2) {
        printf("Incorrect cmd arguments!\n");
        printf("Usage: # ./stream <0/1>\t # 0 -> dax0.0 \t 1 -> dax1.0\n");
        return -1;
    }
    int dax_mode = atoi(argv[1]);
    // print the process id
    printf("Running stream with PID %d\n", getpid());
    // try mounting 
    // we'll use a DAX device to run stream.
    char* uio_mountpoint = (dax_mode == 0) ? "/dev/dax0.0" : "dev/dax1.0";
    // char *uio_mountpoint = (dax_mode == 0) ? "/dev/zero" : "none";

    int uiofd = open(uio_mountpoint, O_RDWR);
    if (uiofd < 0) {
        printf("Error mounting! Make sure that the mount point %s is valid\n",
                uio_mountpoint);
        return EXIT_FAILURE;
    }

    // Allocate the huge page. the starting physical address will be same as
    // the uio device.
    int *ptr = (int *) mmap(
                0x0, 0x100000000, PROT_READ | PROT_WRITE, MAP_SHARED, uiofd, 0);

    // Map should not fail if the device is setup correctly.
    if (ptr == MAP_FAILED) {
        perror("Failed to map a 1 GiB page!\n");
        return -1;
    }
    // there are three pointers. Try doing each array as a 1GiB/
    const size_t num_elements = 0x40000000 / sizeof(int);
    int *ptr_a = &ptr[0];
    int *ptr_b = &ptr[num_elements];
    int *ptr_c = &ptr[2 * num_elements];

    // Map should not fail if the device is setup correctly.
    if (ptr_a == NULL || ptr_b == NULL || ptr_c == NULL) {
        perror("Failed to map a 1 GiB page!\n");
        return -1;
    }

    // Mapping worked. Inform the user where the staring address is located.
    printf("Mapping successful at address %#lx\n", (long) ptr_a);

    // for openmp, if the compiler supports openmp, then then this function
    // call should work. If not (seen in RISCV compilers (old most likely)),
    // then see stream-dax-wo-sync.c on how this part is done.
//    int num_threads = omp_get_num_threads(); 

    // get openmp information here
    size_t num_threads = 0;
#ifdef _OPENMP
#pragma omp parallel
#pragma omp atomic
    num_threads++;
#endif
    printf("Number of threads is %ld\n", num_threads);
    assert(num_threads != 0);


    printf("Number of threads is %d\n", num_threads);

    // double check to make sure that the number of threads is not 0. It'll
    // crash anyway.
    assert(num_threads != 0);

    // Start allocating data into the mmap file. Only the master node will
    // allocate the arrays.
    // There is no need to have a master in this version of the code.

    // This version can jump into this part here.



    // These are the worker threads. the start address will be shifted by
    // the portion that other workers have already worked on.
    
    // now find the start index and end index for this worker
    int worker_start_index = 0;
    int worker_end_index = (STREAM_SIZE);
    
    // allocate pointers for this version of the code. These pointers start at
    // 0, STREAM_SIZE and 2 x STREAM_SIZE.
    int  *A = &ptr_a[0];
    int  *B = &ptr_b[0];
    int  *C = &ptr_c[0];
    // allocate the memory here.
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        A[i] = 1;
        B[i] = 2;
        C[i] = 0;
    }
    
    // After the allocation is complete, we can start the ROI
    printf("this worker %d will start working from %d to %d indices\n",
                        node_id, worker_start_index, worker_end_index);


    // start keeping time
    struct timespec start, stop;
    double time[4];

    // keep start time using start and stop time using stop.
    if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
        perror("error! start time for copy kernel failed.");
        return EXIT_FAILURE;
    }
    // skipping the time
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        C[i] = A[i];
    }

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
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        B[i] = scalar * C[i];
    }

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
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        C[i] = A[i] + B[i];
    }
    
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
#pragma omp parallel for
    for (int i = worker_start_index ; i < worker_end_index ; i++) {
        A[i] = B[i] + scalar * C[i];
    }

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
