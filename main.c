#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PIPE_READ 0
#define PIPE_WRITE 1
#define CLOCK_MONOTONIC 1

typedef int pipe_t;

// Structure to store computation results
typedef struct {
    double sinSum;     // Sum of sine function values from samples
    size_t samples;    // Number of samples
    char padding[64];  // Padding to prevent cache conflicts between threads
} calcInfo;

// Global configuration variables
size_t thread_num;        // Number of threads per process
size_t subproc_num;       // Number of processes
size_t totalSamples;      // Total number of points to generate
size_t samplesPerThread;  // Number of points per thread

// Initializes the result structure
void init_calc_info(calcInfo *info) {
    info->sinSum = 0.0;
    info->samples = 0;
}

// Returns a random value in the range [0, π]
double get_random_input(unsigned int *seed) {
    return ((double) rand_r(seed) / RAND_MAX) * M_PI;
}

// Function executed by each thread
void *compute_sin_sum(void *calcInfo_ptr) {
    // Cast the pointer to the correct type
    calcInfo *info = (calcInfo *) calcInfo_ptr;
    size_t localSamples = samplesPerThread;

    // Initialize the local random seed
    unsigned int seed = getpid() + pthread_self() + time(NULL);

    printf("[Thread %lu - PID %d] started computing %zu points\n", pthread_self(), getpid(), info->samples);

    // Generate points and sum the values of sin(x)
    for (size_t i = 0; i < localSamples; i++) {
        double x = get_random_input(&seed);  // Random x ∈ [0, π]
        info->sinSum += sin(x);              // Add sin(x) value to the sum
        info->samples++;                     // Increment the sample counter
    }
    printf("[Thread %lu - PID %d] finished computing %zu points. Partian sum: %.4lf\n", pthread_self(), getpid(), info->samples, info->sinSum);
    return NULL;
}

// Function called in the child process - creates threads and collects their results
void execute_fork(calcInfo *forkInfo) {
    pthread_t threads[thread_num];        // Array of thread identifiers
    calcInfo threadInfos[thread_num];     // Array of local structures for each thread

    printf("[Process %d] started with %zu threads\n", getpid(), thread_num);
    // Create threads
    for (int i = 0; i < thread_num; i++) {
        init_calc_info(&threadInfos[i]);  // Initialize data for the thread
        pthread_create(&threads[i], NULL, compute_sin_sum, &threadInfos[i]);  // Start the thread
    }

    // Collect results from threads
    for (int i = 0; i < thread_num; i++) {
        pthread_join(threads[i], NULL);  // Wait for the thread to finish

        // Add thread results to the process sum
        forkInfo->sinSum += threadInfos[i].sinSum;
        forkInfo->samples += threadInfos[i].samples;
    }
    printf("[Process %d] finished with %zu threads\n", getpid(), thread_num);
    printf("[Process %d] total sum: %.4lf\n", getpid(), forkInfo->sinSum);
    printf("[Process %d] total samples: %zu\n", getpid(), forkInfo->samples);
}

// Parses program arguments and sets global variables
void read_args(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <number_of_points> <number_of_processes> <number_of_threads>\n", argv[0]);
        exit(1);
    }

    // Convert input arguments to numbers
    totalSamples = atoi(argv[1]);
    subproc_num = atoi(argv[2]);
    thread_num = atoi(argv[3]);

    // Calculate the number of points per thread
    samplesPerThread = totalSamples / (subproc_num * thread_num);
}

// Returns the time in milliseconds since the epoch
long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Displays the program's runtime parameters
void printProcessStatus() {
    printf("\nPROGRAM PARAMETERS\n");
    printf(" - Samples_num: %ld\n", totalSamples);
    printf(" - subprocesses_num:  %ld\n", subproc_num);
    printf(" - threads_num:   %ld\n\n", thread_num);
}

// Main function of the program
int main(int argc, char **argv) {
    read_args(argc, argv);        // Read input parameters

    long timeStart = get_time_ms();  // Measure start time

    pid_t pid;
    pid_t child_pipes[subproc_num];     // Array of child PIDs
    pipe_t pipes[subproc_num][2];       // Array of pipes for communication
    calcInfo globalInfo;                // Structure to sum all results
    init_calc_info(&globalInfo);        // Initialize the structure

    // Create child processes
    for (int i = 0; i < subproc_num; i++) {
        pipe(pipes[i]);              // Create a pipe for communication with the child
        pid = fork();                // Create a child process
        child_pipes[i] = pid;

        if (pid == -1) {
            perror("Failed to create process");
            exit(1);
        } else if (pid == 0) {
            // Child process performs its computations
            calcInfo localInfo;
            init_calc_info(&localInfo);

            execute_fork(&localInfo);  // Start threads and collect their results

            // Send data to the parent process through the pipe
            close(pipes[i][PIPE_READ]);
            write(pipes[i][PIPE_WRITE], &localInfo, sizeof(calcInfo));
            close(pipes[i][PIPE_WRITE]);

            return 0;  // Exit the child process
        }
    }

    // Parent process collects data from children
    for (int i = 0; i < subproc_num; i++) {
        calcInfo temp;

        waitpid(child_pipes[i], NULL, 0);  // Wait for the child to finish

        close(pipes[i][PIPE_WRITE]);      // Close the unused end of the pipe
        read(pipes[i][PIPE_READ], &temp, sizeof(calcInfo));  // Read data
        close(pipes[i][PIPE_READ]);

        // Add data to the global result
        globalInfo.sinSum += temp.sinSum;
        globalInfo.samples += temp.samples;
    }

    printProcessStatus();         // Display them to the user
    printf("[MAIN] finished with %zu subprocesses\n", subproc_num);
    printf("[MAIN] total sum: %.4lf\n", globalInfo.sinSum);
    printf("[MAIN] total samples: %zu\n", globalInfo.samples);

    // Calculate the average value of sin(x)
    double average = globalInfo.sinSum / globalInfo.samples;

    long timeEnd = get_time_ms();  // Measure end time

    double executionTimeSeconds = (timeEnd - timeStart)/1000.0;  // Convert ms to seconds
    double operationsPerSecond = globalInfo.samples / executionTimeSeconds;

    // Display the results
    printf("[MAIN] Execution time: %ld ms\n", timeEnd - timeStart);
    printf("[MAIN] Average value of sin(x) for random x ∈ [0, π]: %lf\n", average);
    printf("[MAIN] Operations per second: %.2lf ops/sec\n", operationsPerSecond);

    return 0;
}