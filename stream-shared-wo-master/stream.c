// This version of stream uses a shared UIO device to execute STREAM. This
// allows multiple independent systems to run a part of stream.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

// This version of stream uses integers

// Each of the stream array will be allocated this much amount of space. The
// mmap call is just one block of memory os size 3 * STREAM_SIZE
#define STREAM_SIZE 1 << 20
#define BILLION 1000000000L

// we need a function to get the offset of within the mmap block to get the
// correct data. The user gives the index of the array.
uint64_t get_offset(char array_name, int index) {
    // For a given array name and the index, we find the correct offset to add
    // to the starting address. There are 3 arrays: a, b, c.
    switch(array_name) {
        case 'a':
        case 'A': return index * sizeof(int);
        case 'b':
        case 'B': return ((STREAM_SIZE) + index) * sizeof(int);
        case 'c':
        case 'C': return ((2 * STREAM_SIZE) + index) * sizeof(int);
        default: printf("error! array name is incorrect.\n");
                exit(-1);
    }
    // returns uint64_t max. add an assert for error handling.
    return -1;
}

int main(int argc, char* argv[]) {

    // The shared version of stream needs to have an invoker ID. There has to
    // be a master node that allocates the stream arrays and workers that
    // run a portion of the kernel. A validation node then checks if the
    // results were correctly computed.
    if (argc != 3) {
        printf("usage: # ./stream-shared <node_id> <total_workers>\n");
        return -1;
    }

    int node_id = atoi(argv[1]);
    int total_workers = atoi(argv[2]);

    if (node_id > total_workers) {
        printf("usage: # ./stream-shared <node_id> <total_workers>\n");
        return -1;
    }

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

    // we'll use a uio device to run stream.
    char* uio_mountpoint = "/dev/uio0";

    int uiofd = open(uio_mountpoint, O_RDWR | O_SYNC);
    if (uiofd < 0) {
        printf("Error mounting! Make sure that the mount point %s is valid\n",
                uio_mountpoint);
        return EXIT_FAILURE;
    }

    // Allocate the huge page. the starting physical address will be same as
    // the uio device.
    char *ptr = mmap(
                0x0, 1 << 30, PROT_READ | PROT_WRITE, MAP_SHARED, uiofd, 0);

    // Map should not fail if the device is setup correctly.
    if (ptr == MAP_FAILED) {
        perror("Failed to map a 1 GiB page!\n");
        return -1;
    }

    // Mapping worked. Inform the user where the staring address is located.
    volatile char* start_address = (volatile char *) ptr;
    printf("Mapping successful at address %#lx\n", (long) start_address);

    // get openmp information here
    size_t num_threads = 0;
#ifdef _OPENMP
#pragma omp parallel
#pragma omp atomic
    num_threads++;
#endif
    printf("Number of threads is %ld\n", num_threads);
    assert(num_threads != 0);

    // Start allocating data into the mmap file. Only the master node will
    // allocate the arrays.
    if (node_id == 0) {
        printf("info: master allocating the stream arrays!\n");
#pragma omp parallel for
        for (int i = 0 ; i < STREAM_SIZE ; i++) {
            *((int *) (start_address + get_offset('A', i))) = 1;
            *((int *) (start_address + get_offset('B', i))) = 2;
            *((int *) (start_address + get_offset('C', i))) = 0;
        }
        printf("info: master allocated the stream arrays!\n");
        // Set the last integer to 1 so that the workers can start working on
        // the data.
        *((int *) (start_address + (1 << 30) - sizeof(int))) = 1;
#ifdef GEM5_ANNOTATION
        system("m5 exit;");
#endif
        printf("arrays allocated and synchronization variable is set!\n");
        // The master will be spinning on the synch variable until all the
        // workers are done working on the data.
        while (1) {
            if (*((int *) (start_address + (1 << 30) - sizeof(int))) == 
                                                            total_workers + 1)
                break;
        }
        printf("workers are done!\n");
    }
    else {
        // take the checkpoint here
#ifdef GEM5_ANNOTATION
        system("m5 exit;");
#endif
        // make sure that synchronization variable is set
        while (1) {
            if (*((int *) (start_address + (1 << 30) - sizeof(int))) == 1)
                break;
        }
        // this worker now can start working

        // These are the worker threads. the start address will be shifted by
        // the portion that other workers have already worked on.
        start_address = start_address + (uint64_t)(node_id - 1) *
                        (uint64_t)(STREAM_SIZE)/(total_workers); 

        // start keeping time
        struct timespec start, stop;
        double time[4];

        // keep start time using start and stop time using stop.
        if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
            perror("error! start time for copy kernel failed.");
            return EXIT_FAILURE;
        }
#pragma omp parallel for
        for (int i = 0 ; i < STREAM_SIZE ; i++) {
            *((int *) (start_address + get_offset('C', i))) =
                                *((int *) (start_address + get_offset('A', i)));
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
        for (int i = 0 ; i < STREAM_SIZE ; i++) {
            *((int *) (start_address + get_offset('B', i))) = scalar *
                                *((int *) (start_address + get_offset('C', i)));
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
        for (int i = 0 ; i < STREAM_SIZE ; i++) {
            *((int *) (start_address + get_offset('C', i))) =
                                *((int *) (start_address + get_offset('A', i))) +
                                *((int *) (start_address + get_offset('B', i)));
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
        for (int i = 0 ; i < STREAM_SIZE ; i++) {
            *((int *) (start_address + get_offset('A', i))) = scalar *
                                *((int *) (start_address + get_offset('B', i))) +
                    scalar * *((int *) (start_address + get_offset('C', i)));
        }

        // keep start time using start and stop time using stop.
        if (clock_gettime(CLOCK_REALTIME, &stop) == -1) {
            perror("error! stop time for triad kernel failed.");
            return EXIT_FAILURE;
        }
        time[3] = (stop.tv_sec - start.tv_sec)
                + (double)(stop.tv_nsec - start.tv_nsec) / (double)BILLION;
        
        ///////////////////////       End of triad kernel      ////////////////////

        printf("warn! the array verifier is not added!\n");
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

        // close a file descriptor for the huge page
        // change the synch variable so that the master can end. this has to
        // be an atomic operation ideally.
        (*((int *) (start_address + (1 << 30) - sizeof(int))))++;
    }
    return 0;
}