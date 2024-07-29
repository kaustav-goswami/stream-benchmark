// This version of stream uses mmap to map a huge 1 GiB page to run stream.
// 

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
#define STREAM_SIZE 1 << 26
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
    // The size of the array is fixed. The program needs to be recompiled
    // when changing the array sizes. Make sure that the array of the stream
    // arrays fit into the hugetlbfs page size
    if ((3 * (STREAM_SIZE)) >= (1 << 30) ) {
        printf("error! arrays don't fit into the huge page!\n");
        return -1;
    }

    // print the process id
    printf("Running stream with PID %d\n", getpid());
    char* hugetlbfs_mountpoint = "/mnt/huge";

    FILE *fp = fopen(hugetlbfs_mountpoint, "w+");
    if (fp == NULL) {
        printf("Error mounting! Make sure that the mount point %s is valid",
                hugetlbfs_mountpoint);
        return EXIT_FAILURE;
    }

    // Try allocating this map into a specific memory address. doesn't really
    // work tho.
    volatile char *force_address = (volatile char *)0x80000000;

    // Allocate the huge page. tlbfs map?
    char *ptr = mmap((void *)force_address, 1 << 30, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | (30UL << MAP_HUGE_SHIFT),
        fileno(fp), 0);

    // Map should not fail if the device is setup correctly.
    if (ptr == MAP_FAILED) {
        perror("Failed to map a hugetlbfs page!");
        fclose(fp);
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

    // Start allocating data into the mmap file.
#pragma omp parallel for
    for (int i = 0 ; i < STREAM_SIZE ; i++) {
        *((int *) (start_address + get_offset('A', i))) = 1;
        *((int *) (start_address + get_offset('B', i))) = 2;
        *((int *) (start_address + get_offset('C', i))) = 0;
    }

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
    fclose(fp);
    return 0;
}