/**
 * mlfq.c
 *
 * PURPOSE: Simulates a Multi-Level Feedback Queue (MLFQ) scheduler that manages tasks and utilizes threads to execute them.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>
#include <time.h>
#include <stdbool.h>
#define NANOS_PER_USEC 1000    // used for diff function
#define USEC_PER_SEC 1000000   // used for diff function
#define MAX_NAME_SIZE 100      // max size of task name
#define QUANTUM_LENGTH 50      // time task runs before inturupt
#define TIME_ALLOTMENT 200     // time before task moved down in priority
#define KEY_WORD_DELAY "DELAY" // keyword to recognize DELAY
#define MAX_FILE_NAME 300      // max length of task file name

// structs

// struct representing each task, which are run on CPUs
typedef struct TASK
{
    // variables needed to store task info
    char name[MAX_NAME_SIZE];
    int length;
    int type;
    int probabilityOfIO;
    int timeRanFor;
    int queuePriority;
    int timeSinceReallotment;
    struct timespec enterTime;     // time task enters MLFQ
    struct timespec scheduleTime;  // time task is ran by CPU for first time
    struct timespec completedTime; // time task is moved to done queue
} Task;

// struct representing nodes in queue data structure
typedef struct QUEUE_NODE
{
    // pointers to other queue nodes
    TAILQ_ENTRY(QUEUE_NODE)
    pointers;

    // task stored in this queue
    Task *task;
} QueueNode;

// function forward declarations
void printStatistics(void);
void *reader(void *arg);
void scheduler(void);
Task *getNext(void);
void *dispatcher(void *arg);
void *cpuThread(void *arg);
void cleanup(void);
struct timespec diff(struct timespec start, struct timespec end);
struct timespec add(struct timespec input1, struct timespec input2);
long double convertToMicroseconds(struct timespec input);
Task *createTask(char name[], int length, int type, int probabilityOfIO, int queuePriority, int timeSinceReallotment);
QueueNode *createQueueNode(Task *task);
static void microsleep(unsigned int usecs);

// variables used to run MLFQ
int sTime;                                                    // time to wait until moving all tasks to priority 1 (in microseconds)
int numOfCPUs;                                                // number of CPUs to run tasks on
int tasksLeft = 1;                                            // boolean value indicating whether or not reader has finished reading all tasks (starts as true)
int cpuQuitSignal = 0;                                        // boolean value indicating whether or not all tasks are completed (start as false)
int firstDelay = 0;                                           // boolean value indicating whether or not reader has encountered the first delay (start as false)
int numOfTasks = 0;                                           // total number of tasks read in so far
int numOfTasksCompleted = 0;                                  // total number of tasks completed so far
int cpuTIDArrayIndex = 0;                                     // index of where next value goes in cpuTIDArrayIndex
char fileName[MAX_FILE_NAME];                                 // name of task file
pthread_t *cpuTIDArray;                                       // array storing CPU thread IDs
pthread_mutex_t doneLock = PTHREAD_MUTEX_INITIALIZER;         // lock for done queue storing completed tasks
pthread_mutex_t waitingQueueLock = PTHREAD_MUTEX_INITIALIZER; // lock for waiting queue
pthread_mutex_t mlfqLock = PTHREAD_MUTEX_INITIALIZER;         // lock for MLFQ (all 4)
pthread_cond_t wakeUpCPUs = PTHREAD_COND_INITIALIZER;         // condition variable for signaling CPUs

// variables used for reporting purposes,
// signify number of each task type
int numOfType0 = 0;
int numOfType1 = 0;
int numOfType2 = 0;
int numOfType3 = 0;

// priority queues
TAILQ_HEAD(priorityOneHead, QUEUE_NODE)
priorityOneQueue;
TAILQ_HEAD(priorityTwoHead, QUEUE_NODE)
priorityTwoQueue;
TAILQ_HEAD(priorityThreeHead, QUEUE_NODE)
priorityThreeQueue;
TAILQ_HEAD(priorityFourHead, QUEUE_NODE)
priorityFourQueue;

// queue for tasks waiting to be put into the MLFQ
TAILQ_HEAD(readyQueueHead, QUEUE_NODE)
waitingQueue;

// queue for completed tasks
TAILQ_HEAD(doneQueueHead, QUEUE_NODE)
doneQueue;

/**
 * Main
 *
 * Initializes variables, sets up the various threads, and calls ending functions
 * @param int argc - number of parameters
 * @param char* argv - command line arguments
 * @returns int - Error or success code
 */
int main(int argc, char *argv[])
{
    // variables for storing TIDs
    pthread_t readerTID;
    pthread_t dispatcherTID;

    // initialize queues
    TAILQ_INIT(&priorityOneQueue);
    TAILQ_INIT(&priorityTwoQueue);
    TAILQ_INIT(&priorityThreeQueue);
    TAILQ_INIT(&priorityFourQueue);
    TAILQ_INIT(&waitingQueue);
    TAILQ_INIT(&doneQueue);

    // read in parameters
    if (argc != 4)
    {
        printf("Incorrect parameters, exiting program. + argc");
        exit(EXIT_FAILURE);
    }

    // copy parameters into storage variables
    numOfCPUs = atoi(argv[1]);
    sTime = atoi(argv[2]);
    strcpy(fileName, argv[3]);

    // set up CPUs thread
    cpuTIDArray = malloc(sizeof(pthread_t) * numOfCPUs);

    // create CPU threads and store their TID
    for (int i = 0; i < numOfCPUs; i++)
    {
        pthread_t cpuTID;

        // create thread for that CPU
        if (pthread_create(&cpuTID, NULL, cpuThread, NULL) != 0)
        {
            printf("Error creating thread, exiting");
            exit(EXIT_FAILURE);
        }

        cpuTIDArray[cpuTIDArrayIndex++] = cpuTID;
    }

    // set up threads
    if (pthread_create(&readerTID, NULL, reader, NULL) != 0)
    {
        printf("Error creating thread, exiting");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&dispatcherTID, NULL, dispatcher, NULL) != 0)
    {
        printf("Error creating thread, exiting");
        exit(EXIT_FAILURE);
    }

    // the main thread now becomes the scheduler
    scheduler();

    // wait for all threads to end
    pthread_join(readerTID, NULL);
    pthread_join(dispatcherTID, NULL);

    // call method for printing statistics
    printStatistics();

    // free all nodes and tasks
    cleanup();

    return EXIT_SUCCESS;
}

/**
 * printStatistics
 *
 * Prints requested statistics regarding turnaround time and response time
 * @returns void - NA
 */
void printStatistics(void)
{
    // variables used for reporting
    QueueNode *node; // used to loop through queues

    struct timespec totalTurnaroundTimeType0 = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec totalTurnaroundTimeType1 = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec totalTurnaroundTimeType2 = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec totalTurnaroundTimeType3 = {.tv_sec = 0, .tv_nsec = 0};

    struct timespec totalResponseTimeType0 = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec totalResponseTimeType1 = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec totalResponseTimeType2 = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec totalResponseTimeType3 = {.tv_sec = 0, .tv_nsec = 0};

    long double averageTurnaroundTimeType0 = 0;
    long double averageTurnaroundTimeType1 = 0;
    long double averageTurnaroundTimeType2 = 0;
    long double averageTurnaroundTimeType3 = 0;

    long double averageResponseTimeType0 = 0;
    long double averageResponseTimeType1 = 0;
    long double averageResponseTimeType2 = 0;
    long double averageResponseTimeType3 = 0;

    // we don't need locks anymore since all other threads except this one have ended

    // iterate through done queue and collect information needed for report
    TAILQ_FOREACH(node, &doneQueue, pointers)
    {
        if (node->task->type == 0)
        {
            totalTurnaroundTimeType0 = add(totalTurnaroundTimeType0, diff(node->task->enterTime, node->task->completedTime));
            totalResponseTimeType0 = add(totalResponseTimeType0, diff(node->task->enterTime, node->task->scheduleTime));
        }
        else if (node->task->type == 1)
        {
            totalTurnaroundTimeType1 = add(totalTurnaroundTimeType1, diff(node->task->enterTime, node->task->completedTime));
            totalResponseTimeType1 = add(totalResponseTimeType1, diff(node->task->enterTime, node->task->scheduleTime));
        }
        else if (node->task->type == 2)
        {
            totalTurnaroundTimeType2 = add(totalTurnaroundTimeType2, diff(node->task->enterTime, node->task->completedTime));
            totalResponseTimeType2 = add(totalResponseTimeType2, diff(node->task->enterTime, node->task->scheduleTime));
        }
        else if (node->task->type == 3)
        {
            totalTurnaroundTimeType3 = add(totalTurnaroundTimeType3, diff(node->task->enterTime, node->task->completedTime));
            totalResponseTimeType3 = add(totalResponseTimeType3, diff(node->task->enterTime, node->task->scheduleTime));
        }
    }

    // do final calculations (make sure to not divide by 0)
    if (numOfType0 != 0)
    {
        averageTurnaroundTimeType0 = convertToMicroseconds(totalTurnaroundTimeType0) / numOfType0;
        averageResponseTimeType0 = convertToMicroseconds(totalResponseTimeType0) / numOfType0;
    }

    if (numOfType1 != 0)
    {
        averageTurnaroundTimeType1 = convertToMicroseconds(totalTurnaroundTimeType1) / numOfType1;
        averageResponseTimeType1 = convertToMicroseconds(totalResponseTimeType1) / numOfType1;
    }

    if (numOfType2 != 0)
    {
        averageTurnaroundTimeType2 = convertToMicroseconds(totalTurnaroundTimeType2) / numOfType2;
        averageResponseTimeType2 = convertToMicroseconds(totalResponseTimeType2) / numOfType2;
    }

    if (numOfType3 != 0)
    {
        averageTurnaroundTimeType3 = convertToMicroseconds(totalTurnaroundTimeType3) / numOfType3;
        averageResponseTimeType3 = convertToMicroseconds(totalResponseTimeType3) / numOfType3;
    }

    // print report
    printf("Using mlfq with %i CPUs and %i microseconds S time.\n\n\n", numOfCPUs, sTime);
    printf("Average turnaround time per type:\n\n\n");
    printf("\tType 0: %Lf usec \n", averageTurnaroundTimeType0);
    printf("\tType 1: %Lf usec \n", averageTurnaroundTimeType1);
    printf("\tType 2: %Lf usec \n", averageTurnaroundTimeType2);
    printf("\tType 3: %Lf usec \n", averageTurnaroundTimeType3);

    printf("\n\n\n");

    printf("Average response time per type:\n\n\n");
    printf("\tType 0: %Lf usec \n", averageResponseTimeType0);
    printf("\tType 1: %Lf usec \n", averageResponseTimeType1);
    printf("\tType 2: %Lf usec \n", averageResponseTimeType2);
    printf("\tType 3: %Lf usec \n", averageResponseTimeType3);

    printf("\n\n\n");
}

/**
 * reader
 *
 * Reads in the tasks from the input file, if encountering a delay then it sleeps for that long. Changes universal variable upon completion of file.
 * @param void* arg - unused
 * @returns void* - unused
 */
void *reader(void *arg)
{
    FILE *taskFile;
    char *line = NULL; // each line of file goes here
    size_t len = 0;    // used for reading in line
    ssize_t read;      // used for reading in line

    // I don't use the parameter but b/c of the compile conditions I get an error if I don't use it
    if (arg == NULL)
    {
        // do nothing
    }

    // open file
    taskFile = fopen(fileName, "r");
    if (taskFile == NULL)
    {
        printf("Can't find file + %s, exiting", fileName);
        exit(EXIT_FAILURE);
    }

    // loop until at end of file
    while ((read = getline(&line, &len, taskFile)) != -1)
    {
        char *token; // used to store result of split string

        // get first
        token = strtok(line, " ");

        // check to see if the first one is DELAY or task name
        if (strcmp(KEY_WORD_DELAY, token) == 0) // it is a delay
        {
            unsigned int timeInMicroseconds;

            // convert length from string to int
            token = strtok(NULL, " ");

            // originally it is in milliseconds, but our function requires microseconds
            timeInMicroseconds = atoi(token);

            // convert to microseconds
            timeInMicroseconds = timeInMicroseconds * 1000;

            // acknowledge that we found the first delay
            firstDelay = 1;

            // sleep for designated time
            microsleep(timeInMicroseconds);
        }
        else // assume that it is a task
        {
            Task *newTask;      // new task
            QueueNode *newNode; // new node to store task
            // variables to store task info
            char name[MAX_NAME_SIZE];
            int taskType;
            int length;
            int ioChance;

            // save name
            strcpy(name, token);

            // save task type
            token = strtok(NULL, " ");
            taskType = atoi(token);

            // save take length
            token = strtok(NULL, " ");
            length = atoi(token);

            // save IO chance
            token = strtok(NULL, " ");
            ioChance = atoi(token);

            // create task
            newTask = createTask(name, length, taskType, ioChance, 1, 0);

            // create new node
            newNode = createQueueNode(newTask);

            // put new node into queue 1
            pthread_mutex_lock(&mlfqLock);
            // set task queue time
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &newNode->task->enterTime);
            TAILQ_INSERT_TAIL(&priorityOneQueue, newNode, pointers);
            pthread_mutex_unlock(&mlfqLock);

            // increment total num of tasks
            numOfTasks++;

            // count what type of task it was
            if (taskType == 0)
            {
                numOfType0++;
            }
            else if (taskType == 1)
            {
                numOfType1++;
            }
            else if (taskType == 2)
            {
                numOfType2++;
            }
            else if (taskType == 3)
            {
                numOfType3++;
            }
        }
    }

    // close file
    fclose(taskFile);
    if (line)
    {
        free(line);
    }

    tasksLeft = 0; // indicate that no tasks remain

    // this return value is not used but I need to return something
    return NULL;
}

/**
 * scheduler
 *
 * Loops until all tasks completed. Responsible for maintaing order of MLFQ. Including moving everything to queue 1 once Stime has elapsed and moving things from waiting queue into the right priority level
 * @returns void - NA
 */
void scheduler(void)
{
    struct timespec startTime;   // last S time reset time
    struct timespec currentTime; // current time

    // first S Time cycle starts now
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &startTime);

    // loop while we still have tasks left to read, or not all tasks are completed
    while (tasksLeft == 1 || numOfTasksCompleted != numOfTasks)
    {
        // get current time
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &currentTime);

        // check S to see if S time has elapsed
        // convert to microseconds then compare to sTime
        if (convertToMicroseconds(diff(startTime, currentTime)) >= sTime)
        {
            // move everything in all priorities to high priority

            // lock mlfq
            pthread_mutex_lock(&mlfqLock);

            // loop through all other queues and move them all to highest priority queue
            while (!TAILQ_EMPTY(&priorityTwoQueue))
            {
                QueueNode *nodeToMove;
                QueueNode *newNode;
                Task *taskToMove;

                // remove node
                nodeToMove = TAILQ_FIRST(&priorityTwoQueue);
                TAILQ_REMOVE(&priorityTwoQueue, nodeToMove, pointers);

                // free node
                taskToMove = nodeToMove->task;
                free(nodeToMove);

                // create new node
                newNode = createQueueNode(taskToMove);

                // put new node into queue
                TAILQ_INSERT_TAIL(&priorityOneQueue, newNode, pointers);
            }

            while (!TAILQ_EMPTY(&priorityThreeQueue))
            {
                QueueNode *nodeToMove;
                QueueNode *newNode;
                Task *taskToMove;

                // remove node
                nodeToMove = TAILQ_FIRST(&priorityThreeQueue);
                TAILQ_REMOVE(&priorityThreeQueue, nodeToMove, pointers);

                // free node
                taskToMove = nodeToMove->task;
                free(nodeToMove);

                // create new node
                newNode = createQueueNode(taskToMove);

                // put new node into queue
                TAILQ_INSERT_TAIL(&priorityOneQueue, newNode, pointers);
            }

            while (!TAILQ_EMPTY(&priorityFourQueue))
            {
                QueueNode *nodeToMove;
                QueueNode *newNode;
                Task *taskToMove;

                // remove node
                nodeToMove = TAILQ_FIRST(&priorityFourQueue);
                TAILQ_REMOVE(&priorityFourQueue, nodeToMove, pointers);

                // free node
                taskToMove = nodeToMove->task;
                free(nodeToMove);

                // create new node
                newNode = createQueueNode(taskToMove);

                // put new node into queue
                TAILQ_INSERT_TAIL(&priorityOneQueue, newNode, pointers);
            }

            // unlock mlfq
            pthread_mutex_unlock(&mlfqLock);

            // reset S time because we have just moved everything to queue 1
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &startTime);
        }

        // empty waiting queue containing tasks waiting to be put into MLFQ
        // loop until we have taken out everything and put it into queue level identified in struct
        while (!TAILQ_EMPTY(&waitingQueue))
        {
            QueueNode *nodeToMove;
            QueueNode *newNode;
            Task *taskToMove;

            // lock waiting queue and move the tasks into regular queues
            pthread_mutex_lock(&waitingQueueLock);

            // remove node
            nodeToMove = TAILQ_FIRST(&waitingQueue);
            TAILQ_REMOVE(&waitingQueue, nodeToMove, pointers);

            pthread_mutex_unlock(&waitingQueueLock);

            // free node
            taskToMove = nodeToMove->task;
            free(nodeToMove);

            // calculate whether not to move node to a lower queue
            if (taskToMove->timeSinceReallotment > TIME_ALLOTMENT)
            {
                // adjust queue priority down
                if (taskToMove->queuePriority != 4)
                {
                    taskToMove->queuePriority = taskToMove->queuePriority - 1;
                }

                // reset time
                taskToMove->timeSinceReallotment = 0;
            }

            // create new node
            newNode = createQueueNode(taskToMove);

            // put new node into queue
            if (newNode->task->queuePriority == 1)
            {
                pthread_mutex_lock(&mlfqLock);
                TAILQ_INSERT_TAIL(&priorityOneQueue, newNode, pointers);
                pthread_mutex_unlock(&mlfqLock);
            }
            // put node into queue 2
            else if (newNode->task->queuePriority == 2)
            {
                pthread_mutex_lock(&mlfqLock);
                TAILQ_INSERT_TAIL(&priorityTwoQueue, newNode, pointers);
                pthread_mutex_unlock(&mlfqLock);
            }
            // put node into queue 3
            else if (newNode->task->queuePriority == 3)
            {
                pthread_mutex_lock(&mlfqLock);
                TAILQ_INSERT_TAIL(&priorityThreeQueue, newNode, pointers);
                pthread_mutex_unlock(&mlfqLock);
            }
            // put node into queue 4
            else
            {
                pthread_mutex_lock(&mlfqLock);
                TAILQ_INSERT_TAIL(&priorityFourQueue, newNode, pointers);
                pthread_mutex_unlock(&mlfqLock);
            }
        }
    }

    // switch variable telling CPUs to end
    cpuQuitSignal = 1;

    // grab lock before trying to wake them up
    pthread_mutex_lock(&mlfqLock);

    // send signal to wake all of them up
    pthread_cond_broadcast(&wakeUpCPUs);
    pthread_mutex_unlock(&mlfqLock);

    // wait for all CPUs to end
    for (int i = 0; i < numOfCPUs; i++)
    {
        pthread_join(cpuTIDArray[i], NULL);
    }

    // free memory with CPU ids since we don't need them anymore
    free(cpuTIDArray);
}

/**
 * getNext
 *
 * Looks in MLFQ and returns highest priority task (or NULL if nothing is in queue, but this shouldn't happen). No locks used b/c CPU thread should have it when this is called.
 * @returns Task* - Highest priority task in MLFQ (or NULL if nothing in MLFQ)
 */
Task *getNext(void)
{
    // NOTE: there are no locks in this section because it is only ever called by the CPUs (who have gotten the lock already)

    Task *nextTask;
    QueueNode *nextNode;

    // check whether queue1 is empty. If so then move onto next priority level. If not then return first node in queue
    if (!TAILQ_EMPTY(&priorityOneQueue))
    {
        // get node
        nextNode = TAILQ_FIRST(&priorityOneQueue);
        TAILQ_REMOVE(&priorityOneQueue, nextNode, pointers);

        // free node
        nextTask = nextNode->task;
        free(nextNode);

        return nextTask;
    }
    // check whether queue2 is empty. If so then move onto next priority level. If not then return first node in queue
    else if (!TAILQ_EMPTY(&priorityTwoQueue))
    {
        // get node
        nextNode = TAILQ_FIRST(&priorityTwoQueue);
        TAILQ_REMOVE(&priorityTwoQueue, nextNode, pointers);

        // free node
        nextTask = nextNode->task;
        free(nextNode);

        return nextTask;
    }
    // check whether queue3 is empty. If so then move onto next priority level. If not then return first node in queue
    else if (!TAILQ_EMPTY(&priorityThreeQueue))
    {
        // get node
        nextNode = TAILQ_FIRST(&priorityThreeQueue);
        TAILQ_REMOVE(&priorityThreeQueue, nextNode, pointers);

        // free node
        nextTask = nextNode->task;
        free(nextNode);

        return nextTask;
    }
    // check whether queue4 is empty. If so then move onto next priority level. If not then return first node in queue
    else if (!TAILQ_EMPTY(&priorityFourQueue))
    {
        // get node
        nextNode = TAILQ_FIRST(&priorityFourQueue);
        TAILQ_REMOVE(&priorityFourQueue, nextNode, pointers);

        // free node
        nextTask = nextNode->task;
        free(nextNode);

        return nextTask;
    }

    // else return null
    return NULL;
}

/**
 * dispatcher
 *
 * Loops until told to stop. Checks to see if there is anything in MLFQ, if so then it signals CPUs to grab one.
 * @param void* arg - unused
 * @returns void* - unused
 */
void *dispatcher(void *arg)
{
    // I don't use the parameter but b/c of the compile conditions I get an error if I don't use it
    if (arg == NULL)
    {
        // do nothing
    }

    // loop until told to quit
    while (!cpuQuitSignal)
    {
        // get lock
        pthread_mutex_lock(&mlfqLock);

        // check whether any of the queues are not empty and if we have processed the first delay
        if (
            firstDelay == 1 &&
            (!TAILQ_EMPTY(&priorityOneQueue) || !TAILQ_EMPTY(&priorityTwoQueue) || !TAILQ_EMPTY(&priorityThreeQueue) || !TAILQ_EMPTY(&priorityFourQueue)))
        {
            // if so, signal the CPUs to wake up and grab something
            pthread_cond_signal(&wakeUpCPUs);
        }

        // unlock
        pthread_mutex_unlock(&mlfqLock);
    }

    // not used but I have to return something
    return NULL;
}

/**
 * cpuThread
 *
 * Individual CPU thread. Waits until told job is available, gets next job, runs it, and then either puts it in done queue or waiting queue
 * @param void* arg - unused
 * @returns void* - unused
 */
void *cpuThread(void *arg)
{
    // parameter not used but I must use parameter else it won't compile
    if (arg == NULL)
    {
        // do nothing
    }

    // while it has not been signaled to quit
    while (!cpuQuitSignal)
    {
        Task *myTask = NULL;

        // wait for signal to grab new task
        // either it gets signal to grab new task or it gets signal to end
        pthread_mutex_lock(&mlfqLock);

        // grab next task (or null)
        myTask = getNext();
        // loop until new task available or told to quit
        while (myTask == NULL && !cpuQuitSignal)
        {
            pthread_cond_wait(&wakeUpCPUs, &mlfqLock);
            // try to get new task
            myTask = getNext();
        }

        // unlock to run task
        pthread_mutex_unlock(&mlfqLock);

        // if quit signal is still false that means that we have a task to work on
        if (!cpuQuitSignal)
        {
            // queue to store our task once we are done with it
            QueueNode *newNode;
            // variable storing amount of time task will "run" this time
            int runTime;

            // check whether task has been run yet
            if (myTask->timeRanFor == 0)
            {
                // if not, update variable for reporting purposes
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &myTask->scheduleTime);
            }

            // generate random number
            int upper = 100;
            int lower = 0;
            double randomNumber = (rand() % (upper - lower + 1)) + lower;

            // if less than or equal to probability of IO, we do IO
            if (randomNumber <= myTask->probabilityOfIO)
            {
                // generate another random number
                upper = QUANTUM_LENGTH;
                lower = 0;
                // this runtTime number is how much time the task has left to run now that IO is finished
                runTime = (rand() % (upper - lower + 1)) + lower;
            }
            // otherwise it just runs for its complete time slice
            else
            {
                runTime = QUANTUM_LENGTH;
            }

            // check whether task has enough time left to run to justify full length
            if ((myTask->length - myTask->timeRanFor) < runTime)
            {
                // if not then it only runs for amount of time it has left
                runTime = (myTask->length - myTask->timeRanFor);
            }

            // run task
            microsleep(runTime);

            // count how long task has run for
            myTask->timeRanFor = myTask->timeRanFor + runTime;

            // update variable detailing how long it was run since last being demoted
            myTask->timeSinceReallotment = myTask->timeSinceReallotment + runTime;

            // make new node for our task
            newNode = createQueueNode(myTask);

            // if task finished move to done
            if (myTask->timeRanFor == myTask->length)
            {
                pthread_mutex_lock(&doneLock);
                // update time in variable for reporting purposes
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &myTask->completedTime);
                TAILQ_INSERT_TAIL(&doneQueue, newNode, pointers);
                pthread_mutex_unlock(&doneLock);

                // increment variable counting number of tasks that have been completed
                numOfTasksCompleted++;
            }
            // else put back in waiting queue
            else
            {
                pthread_mutex_lock(&waitingQueueLock);
                TAILQ_INSERT_TAIL(&waitingQueue, newNode, pointers);
                pthread_mutex_unlock(&waitingQueueLock);
            }
        }

        // if signal is not false that means there are no more tasks and to just end
    }

    // return value not used but I have to return something
    return NULL;
}

/**
 * cleanup
 *
 * Frees memory used by queue nodes and tasks
 * @returns void - NA
 */
void cleanup(void)
{
    QueueNode *item;
    while (!TAILQ_EMPTY(&doneQueue))
    {
        item = TAILQ_FIRST(&doneQueue);
        TAILQ_REMOVE(&doneQueue, item, pointers);
        free(item->task);
        free(item);
    }
}

/**
 * diff
 *
 * Calculates time difference between two timespec structs then returns new timespec representing that difference
 * @param timespec start - starting time (smaller time)
 * @param timespec end - ending time (larger time)
 * @returns timespec - timespec struct containing difference between two parameters
 */
struct timespec diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0)
    {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    }
    else
    {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

/**
 * add
 *
 * Adds two timespecs together and returns result as timespec
 * @param timespec input1 - first timespec to add
 * @param timespec input2 - second timespec to add
 * @returns timespec - timespec struct containing two input structs added together
 */
struct timespec add(struct timespec input1, struct timespec input2)
{
    struct timespec temp;

    // check whether when added together the nanoseconds make a full second
    if ((input1.tv_nsec + input2.tv_nsec) >= 1000000000)
    {
        temp.tv_sec = input1.tv_sec + input2.tv_sec + 1;
        temp.tv_nsec = input1.tv_nsec + input2.tv_nsec - 1000000000;
    }
    else
    {
        temp.tv_sec = input1.tv_sec + input2.tv_sec;
        temp.tv_nsec = input1.tv_nsec + input2.tv_nsec;
    }
    return temp;
}

/**
 * convertToMicroseconds
 *
 * Converts timespec struct to microseconds and returns it
 * @param timespec input - timespec to convert
 * @returns long double - timespec in microseconds
 */
long double convertToMicroseconds(struct timespec input)
{
    // convert seconds and nanoseconds to microseconds and add them
    long secondsToMicroseconds = input.tv_sec * 1000000;
    long double nanosecondsToMicroseconds = input.tv_nsec / 1000;
    long double microseconds = secondsToMicroseconds + nanosecondsToMicroseconds;

    return microseconds;
}

/**
 * createTask
 *
 * Takes parameters for new task and allocates memory then assigns parameters to new task
 * @param char[] name - name for task
 * @param int length - length for task
 * @param int type - type of task
 * @param int probabilityOfIO - IO probability of task
 * @param int queuePriority - queue priority of task (usually starts at 1)
 * @param int timeSinceReallotment - time since task was last moved down in priority (starts at 0)
 * @returns Task* - new Task created
 */
Task *createTask(char name[], int length, int type, int probabilityOfIO, int queuePriority, int timeSinceReallotment)
{
    Task *task = (Task *)malloc(sizeof(Task));
    strcpy(task->name, name);
    task->length = length;
    task->type = type;
    task->probabilityOfIO = probabilityOfIO;
    task->queuePriority = queuePriority;
    task->timeSinceReallotment = timeSinceReallotment;
    task->timeRanFor = 0;
    return task;
}

/**
 * createQueueNode
 *
 * Takes parameters for new queue node and allocates memory then assigns parameters to new node
 * @param TASK* task - task to be stored in node
 * @returns QueueNode* - new node
 */
QueueNode *createQueueNode(Task *task)
{
    QueueNode *queueNode = (QueueNode *)malloc(sizeof(QueueNode));
    queueNode->task = task;
    return queueNode;
}

/**
 * microsleep
 *
 * Sleeps for the number of microseconds specified in parameter
 * @param unsigned int usecs - number of microseconds to sleep
 * @returns void - NA
 */
static void microsleep(unsigned int usecs)
{
    long seconds = usecs / USEC_PER_SEC;
    long nanos = (usecs % USEC_PER_SEC) * NANOS_PER_USEC;
    struct timespec t = {.tv_sec = seconds, .tv_nsec = nanos};
    int ret;
    do
    {
        ret = nanosleep(&t, &t);
        // need to loop, `nanosleep` might return before sleeping
        // for the complete time (see `man nanosleep` for details)
    } while (ret == -1 && (t.tv_sec || t.tv_nsec));
}
