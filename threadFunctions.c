#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <unistd.h>

#include <pthread.h>

#include <semaphore.h>

#include <time.h>

#include "obtainCpuStatistics.h"

#include "getCpuInfo.h"

#include "calculations.h"

#include "threadFunctions.h"

//mutexes and semaphores defined
pthread_mutex_t mutexCheckReadData;

pthread_mutex_t mutexWatchdog;

pthread_cond_t condWatchdog;

pthread_mutex_t mutexLogger;

pthread_cond_t condLogger;

sem_t semReaderEmpty;

sem_t semReaderFull;

sem_t semAnalyzerEmpty;

sem_t semAnalyzerFull;

int signalChecker = 0;

static long watchTime = 0;

//if signal for termination is received - change to 1 which will lead to closing of all threads
void signalCheck(int arg)
{
    (void)arg;

    signalChecker = 1;
    printf("\nExiting program...\n");
}

//Run getDataFromFile every second and update the matrix with information
void *runReader(void *arg)
{
    (void)arg;

    //alloc matrixes
    cpuCoresAsMatrix = malloc((unsigned long)numberOfCpus * sizeof(int *));

    for (int i = 0; i < numberOfCpus; i++)
    {
        cpuCoresAsMatrix[i] = malloc((unsigned long)numberOfStatistics * sizeof(int));
    }

    cpuCoresAsMatrixOld = malloc((unsigned long)numberOfCpus * sizeof(int *));

    for (int i = 0; i < numberOfCpus; i++)
    {
        cpuCoresAsMatrixOld[i] = malloc((unsigned long)numberOfStatistics * sizeof(int));
    }

    //run until signal detected
    while(signalChecker == 0)
    {
        //wait for semaphore
        sem_wait(&semReaderEmpty);

        pthread_mutex_lock(&mutexCheckReadData);

        //write old data to the matrix
        getOldDataFromFile();

        if (signalChecker == 0)
            sleep(1);
        //write current data to the matrix
        getDataFromFile();

        pthread_mutex_unlock(&mutexCheckReadData);

        //post semaphore
        sem_post(&semReaderFull);
    }

    //free allocated matrixes
    for (int i = 0; i < numberOfCpus; i++)
    {
        free(cpuCoresAsMatrix[i]);
    }

    free(cpuCoresAsMatrix);

    for (int i = 0; i < numberOfCpus; i++)
    {
        free(cpuCoresAsMatrixOld[i]);
    }
    free(cpuCoresAsMatrixOld);

    return 0;
}

//calculates CPU usage percentage
void *runAnalyzer(void *arg)
{
    (void)arg;

    //allocate memory for CPU_percentage
    CPU_Percentage = malloc((unsigned long)numberOfCpus * sizeof(int));

    //run until signal detected
    while(signalChecker == 0)
    {
        sem_wait(&semReaderFull);

        sem_wait(&semAnalyzerEmpty);

        pthread_mutex_lock(&mutexCheckReadData);

        //Run calculate function
        if (signalChecker == 0)
            calculateCpuUsage();

        pthread_mutex_unlock(&mutexCheckReadData);

        sem_post(&semReaderEmpty);

        sem_post(&semAnalyzerFull);
    }
    //free up memory allocated to CPU_Percentage
    free(CPU_Percentage);

    return 0;
}

//Prints out CPU Percentage
void *runPrinter(void *arg)
{
    (void)arg;

    //run until signal detected
    while(signalChecker == 0)
    {
        sem_wait(&semAnalyzerFull);

        if (signalChecker == 0)
        {
            for (int i = 0; i < numberOfCpus; i++)
            {
                if (i == 0)
                {
                    printf("Total CPU usage: %.02f%%\n", (double)CPU_Percentage[i]);
                }
                else
                {
                    printf("CPU %d usage:     %.02f%%\n", i, (double)CPU_Percentage[i]);
                }
            }
            printf("\n");
        }
        sem_post(&semAnalyzerEmpty);

        pthread_cond_signal(&condWatchdog);

        pthread_cond_signal(&condLogger);
    }
    return 0;
}

//whenever thread finishes, it sends signal to the conditional variable. If the time between signals is longer then 2 seconds - watchdog closes the program
void *runWatchdog(void *arg)
{
    (void)arg;

    //run until signal detected
    while(signalChecker == 0)
    {
        //read starting time
        time_t ReaderBegin = time(NULL);

        //check for signal from threads
        if(signalChecker == 0)
            pthread_cond_wait(&condWatchdog, &mutexWatchdog);

        //read end time
        time_t ReaderEnd = time(NULL);

        watchTime = ReaderEnd - ReaderBegin;

        //save time or close the program depending on the difference in time
        if (watchTime >= 2)
        {
            printf("Time waiting for a threads exceeded 2 seconds... Exiting program\n");
            signalChecker = 2;
        }
    }
    return 0;
}

//after every thread loop - write related information into a log file
void *runLogger(void *arg)
{
    (void)arg;

    //create a file pointer, open a file and write into it
    FILE *fptr;

    fptr = fopen("Log", "w");

    fprintf(fptr, "\nLog start\n");

    //basic information
    fprintf(fptr, "\nNo of CPUs detected: %d, number of statistics detected: %d\n", numberOfCpus, numberOfStatistics);

    //loop information
    while(signalChecker == 0)
    {
        pthread_cond_wait(&condLogger, &mutexLogger);

        if (signalChecker == 0)
        {
            for (int i = 0; i < numberOfCpus; i++)
            {
                if (i == 0)
                {
                    fprintf(fptr, "Total CPU usage: %.02f%%\n", (double)CPU_Percentage[i]);
                }
                else
                {
                    fprintf(fptr, "CPU %d usage:     %.02f%%\n", i, (double)CPU_Percentage[i]);
                }
            }
            fprintf(fptr, "\n");

            fprintf(fptr, "The elapsed time for threads is %ld seconds\n\n", (watchTime));
        }
    }

    //if threads hang up for at least 2 seconds
    if (signalChecker == 2)
    {
        fprintf(fptr, "Time waiting for a threads exceeded 2 seconds... Exiting program\n");
    }

    //close the file
    fclose(fptr);

    return 0;
}
