//
//  main.c
//  Evgenii_Litvinov_AirlineTickets
//  Purpouse for the assigment: Learn how to use LLF, EDF Scheduling, and semaphore to synchronize access to a shared memory segment.
//  Code was written in C language.
//
//

#include <stdio.h>       /* I/O                                  */
#include <sys/types.h>   /* various type definitions.            */
#include <sys/ipc.h>     /* general SysV IPC structures          */
#include <sys/shm.h>     /* semaphore functions and structs      */
#include <sys/sem.h>     /* shared memory functions and structs. */
#include <unistd.h>      /* fork()                               */
#include <sys/wait.h>    /* wait()                               */
#include <time.h>        /* nanosleep()                          */
#include <stdlib.h>      /* rand()                               */
#include <string.h>
#include <stdbool.h>

//declare the constant variables
#define SIZE 1024
#define SUCCESS 0
#define FAILURE 1
#define KEY 1827937
#define SEM_ID    250          /* ID for the semaphore.           */
#define MAX_AGENTS 10          /* No more than this many agents   */
#define MAX_FLIGHTS 15
#define MAX_TRANSACTIONS 20    /* Max # transactions per agent    */
#define MAX_ROWS 50
#define MAX_SEATS 1


/* Assign constant names */
enum TransType { Reserve, Wait, Waitany, Ticket, Cancel, Check, LastTrans };
char *TransNames[LastTrans] = { "reserve", "wait", "waitany", "ticket", "cancel", "check_passenger" };

struct Transaction {
    enum TransType type;        /* transaction type */
    char flight[10];            /* flight name */
    int rowNum, seatNum;        /* row and seat numbers. Not used for check transaction */
    char name[10];              /* passenger name */
    int deadline;
};

/* define a structure to be used in shared memory segment. */
struct Flight {
    char name[10];
    int numRows, numSeats;
    int numOfbytes;
    char data[SIZE];
    char seats[MAX_ROWS][MAX_SEATS][10]; /* names of passengers in the seats. empty means not occupied */
};

struct Agent {
    char name[20];
    int executionTime[LastTrans]; /* execution time for each transaction */
    struct Transaction transactions[MAX_TRANSACTIONS];
    int numTransactions;
};

bool readFlight(FILE *fp, struct Flight *f)
{
    /* Example: SA113 6 10 */
    memset(f, 0, sizeof(*f));    /* clear it out */
    int res = fscanf(fp, "%s %d %d\n", f->name, &f->numRows, &f->numSeats);
    return (res == 3);
}

/* Convert a transaction name to its type. */
enum TransType nameToTransType(char *name)
{
    for (int i=0; i<LastTrans; ++i) {
    if (strcmp(TransNames[i], name) == 0) return i;
    }
    printf("%s isn't a transaction name\n", name);
    return Reserve;        /* gotta return something */
}

/* Read an agent's data from the file */
bool readAgent(FILE *fp, struct Agent *a)
{
    char name[20];        /* transaction name */
    char seatChar;        /* seat within a row (A, B, C, etc) */
    int execTime;
    
    memset(a, 0, sizeof(*a));
    
    fscanf(fp, "%s:\n", a->name);

    /* Read the transaction times */
    for (int i=0; i<LastTrans; ++i) {
    if (fscanf(fp, "%s %d\n", name, &execTime) != 2) return false;
    a->executionTime[nameToTransType(name)] = execTime;
    printf("%s -> %d\n", name, execTime);
    }
    printf("Reading the transactions\n");
    /* Now read the transactions */
    while (fscanf(fp, "%s", name) == 1) {
    printf("Reading trans %s\n", name);
    struct Transaction *tp = &a->transactions[a->numTransactions]; /* shorthand */
    if (strcmp(name, "end.") == 0) break; /* end of input */
    tp->type = nameToTransType(name);
    if (tp->type == Check) {
        if (fscanf(fp, "%s deadline %d\n", tp->name, &tp->deadline) != 2) return false;
    } else {
        if (fscanf(fp, "%s %d%c %s deadline %d\n", tp->flight, &tp->rowNum, &seatChar, tp->name, &tp->deadline) != 5) return false;
        tp->seatNum = seatChar - 'A';
    }
    /* success! */
    ++a->numTransactions;
    }
    return true;
}
void waitChild(void);

/* Delay the executing process for a random number of nano-seconds. */
void random_delay() {
    static int initialized = 0;
    int random_num;
    struct timespec delay;            /* used for wasting time. */
    if (!initialized) {
        srand(time(NULL));
        initialized = 1;
    }
    random_num = rand() % 10;
    delay.tv_sec = 0;
    delay.tv_nsec = 10 * random_num;
    nanosleep(&delay, NULL);
}

 /* locks the semaphore, for exclusive access to a resource.*/
void sem_lock(int sem_set_id) {
    /* structure for semaphore operations. */
    struct sembuf sem_op;

    /* wait on the semaphore, unless it's value is non-negative. */
    sem_op.sem_num = 0;
    sem_op.sem_op = -1;
    sem_op.sem_flg = 0;
    semop(sem_set_id, &sem_op, 1);
}

/* function: sem_unlock. un-locks the semaphore.*/
void sem_unlock(int sem_set_id) {
    /* structure for semaphore operations.   */
    struct sembuf sem_op;

    /* signal the semaphore - increase its value by one. */
    sem_op.sem_num = 0;
    sem_op.sem_op = 1;
    sem_op.sem_flg = 0;
    semop(sem_set_id, &sem_op, 1);
}
//Earliest Deadline First (EDF) Scheduling
int gcd(int a,int b){
    
    if(b==0)
        return a;
    else
        gcd(b,a%b);
    return 0;
}
    
int lcm(int a,int b){
    return((a*b)/gcd(a,b));
}
int hyperperiod(float period[],int n){
    int k=period[0];
    while(n>=1){
        k=lcm(k,period[n]);}
    return k;
}

int edf(float *period,int n,int t,float *deadline){
    
    int i,small=10000.0f,smallindex=0;
    for(int i=0;i<n;i++){
        if(period[i]<small&&(period[i]-t)<=deadline[i]){
            small=period[i];
            smallindex=i;
        }
    }
    if(small==10000.0f)
        return -1;
    return smallindex;
}

// Least Laxity First (LLF) Scheduling

int llf(float *period,int n,int t,float *deadline,float *timeremaining){
    int i,small=10000,smallindex=0;
    for(i=0;i<n;i++)
    {
        if(((period[i]-t-timeremaining[i])<small)&&(period[i]-t)<=deadline[i])
        {
            small=period[i]-t-timeremaining[i];
            smallindex=i;
        }
        
    }
    
    if(small==10000)
        return -1;
        return smallindex;
}

/* Main function */
int main(int argc, char* argv[]) {
    int sem_set_id;               /* ID of the semaphore               */
    char data[SIZE];
    FILE *inFile;
    int shmid;                     /* ID of the shared memory segment  */
    char* shm_addr;                /* address of shared memory segment */
    int* flight_info;              /* number of flightes in shared mem */
    struct flight* flightes;       /* flightes array in shared mem     */
    struct shmid_ds shm_desc;
    int rc;                        /* return value of system calls     */
    pid_t pid = 0;                 /* PID of child process             */
    key_t sharedKey = KEY;
    void *ptr = (void *)0;
    struct Flight*sharedMemory;
    union semun {                 /* semaphore value, for semctl()      */
        int val;
        struct semid_ds* buf;
        ushort* array;
    } sem_val;
    
    
    /* Create a shared memory segment and print an error if shmget fails */
    if ((sem_set_id = shmget(sharedKey, sizeof(struct Flight), IPC_CREAT | 0666)) < 0)

    {
        perror("shmget");
        
        exit(FAILURE);
    }

    /* Attach the shared memory.Call the function shmat(),and print an error if shmget fails */

    if ((ptr = shmat(sem_set_id, (void *)0, 0)) == (void *)-1)

    {
        perror("shmat");

        exit(FAILURE);
    }
    /* Convert the sharedMemory into the structure type */
    sharedMemory = (struct Flight *) ptr;
    /* Set the numOfbytes to 0 */
    sharedMemory->numOfbytes = 0;

    /* read the input file */
    while (fgets(data, (SIZE - 1), inFile) != NULL)
    {
        /* call the function waitChild() */
        //wait for the child to read from shared memory
        while (sharedMemory->numOfbytes != 0){
            waitChild();
        }
        /* copy the read data to the sharedMemory */
        strcpy(sharedMemory->data, data);
        /* set the numOfbytes */
        sharedMemory->numOfbytes = strlen(sharedMemory->data);
    }
    
    /* call the function waitChild() */
    /* and wait for the child to read from shared memory */
    while (sharedMemory->numOfbytes != 0)
    {
        waitChild();
    }
    /* set the numOfbytes */
    sharedMemory->numOfbytes = -1;
    /*call the function waitpid() */
    waitpid(pid, NULL, 0);
    /*Close the input file. */
    fclose(inFile);
    /*call the shmdt() */
    if (shmdt(sharedMemory) == -1)
    {
    perror("shmdt");
    exit(FAILURE);
    }
    
    
    /* Read the input from stdin. If it's in a file then open the
       file and read from there. I'm reading the data into local
       arrays. */
    struct Flight localFlights[MAX_FLIGHTS];
    struct Agent localAgents[MAX_AGENTS];
    int numAgents, numFlights;
    fscanf(stdin, "%d", &numFlights);
    for (int i=0; i<numFlights; ++i) {
    if (!readFlight(stdin, &localFlights[i])) {
        printf("Can't read flight %d", i);
        return 1;
    }
    }
    fscanf(stdin, "%d", &numAgents);
    for (int i=0; i<numAgents; ++i) {
    if (!readAgent(stdin, &localAgents[i])) {
        printf("Can't read agent %d", i);
        return 1;
    }
    }
    return 0;

    
#if 0
    /* create a semaphore set with ID 250, with one semaphore   */
    /* in it, with access only to the owner.                    */
    sem_set_id = semget(SEM_ID, 1, IPC_CREAT | 0600);
    if (sem_set_id == -1) {
        perror("main: semget");
        exit(1);
    }

    /* intialize the first (and single) semaphore in our set to '1'. */
    sem_val.val = 1;
    rc = semctl(sem_set_id, 0, SETVAL, sem_val);
    if (rc == -1) {
        perror("main: semctl");
        exit(1);
    }

    /* allocate a shared memory segment with size of 2048 bytes. */
    shmid = shmget(100, 2048, IPC_CREAT | IPC_EXCL | 0600);
    if (shmid == -1) {
        perror("main: shmget: ");
        exit(1);
    }

    /* attach the shared memory segment to our process's address space. */
    shm_addr = (char*)shmat(shmid, NULL, 0);
    if (!shm_addr) { /* operation failed. */
        perror("main: shmat: ");
        exit(1);
    }

    /* create a flightes index on the shared memory segment. */
    flight_info = (int*)shm_addr;
    *flight_info = 0;
    flightes = (struct flight*)((void*)shm_addr + sizeof(int));

    /* fork-off a child process that'll populate the memory segment. */
    pid = fork();
    switch (pid) {
    case -1:
        perror("fork: ");
        exit(1);
        break;
    case 0:
        agent2(sem_set_id, flight_info, flightes);
        exit(0);
        break;
    default:
        agent1(sem_set_id, flight_info, flightes);
        break;
    }

    /* wait for child process's terination. */
    {
        int child_status;

        wait(&child_status);
    }

    /* detach the shared memory segment from our process's address space. */
    if (shmdt(shm_addr) == -1) {
        perror("main: shmdt: ");
    }

    /* de-allocate the shared memory segment. */
    if (shmctl(shmid, IPC_RMID, &shm_desc) == -1) {
        perror("main: shmctl: ");
    }
#endif
    return 0;
    
    //(EDF) Scheduling
    int i,n,c,d,k,j,nexttime=0,time=0,task,preemption_count;
    float exec[20],period[20],individual_util[20],flag[20],release[20],
    deadline[20],instance[20],ex[20],responsemax[20],responsemin[20],
    tempmax;
    float util=0;
    printf("Earliest Deadline First Algorithm");

    FILE *read;
    fscanf(read,"%d ",&n);
    for(i=0;i<n;i++)
    {
    fscanf(read,"%f ",&release[i]);
    fscanf(read,"%f ",&period[i]);
    fscanf(read,"%f ",&exec[i]);
    fscanf(read,"%f ",&deadline[i]);
    }
    fclose(read);
    for(i=0;i<n;i++)
    {
    individual_util[i]=exec[i]/period[i];
    util+=individual_util[i];
    responsemax[i]=exec[i];
    deadline[i]=period[i];
    instance[i]=0.0f;
    }
    util=util*100;
    if(util>100)
    printf("\nUtilisation factor = %0.2f ",util);
    else
    {
    printf("\nUtilisation factor = %0.2f",util);
    printf("\nHyperperiod of the given task set is : %d",k=hyperperiod(period , n));
    c=0;
    while(time<k)
    {
    nexttime=time+1;
    task = edf(period,n,time,deadline);
    if(task==-1)
    {
    printf("-");
    time++;
    continue;
    }
    instance[task]++;
    printf("T%d ",task);
    ex[c++]=task;
    if(instance[task]==exec[task])
    {
    tempmax=nexttime-(period[task]-deadline[task]);
    if(instance[task]<tempmax)
    {
    responsemax[task]=tempmax;
    }
    else
    {
    responsemin[task]=instance[task];
    }
    if(deadline[task]==k)
    {
    responsemin[task]=responsemax[task];
    }
    period[task]+=deadline[task];
    instance[task]=0.0f;
    }

    time++;
    }
    for(i=0;i<n;i++)
    {
    printf("\nMaximum Response time of Task %d = %f",i,responsemax[i]);
    printf("\nMinimum Response time of Task %d = %f",i,responsemin[i]);
    }

    preemption_count=0;
    for(i=0;i<k;i=j)
    {
    flag[i]=1;
    d=ex[i];
    for(j=i+1;d==ex[j];j++)
    flag[d]++;
    if(flag[d]==exec[d])
    flag[d]=1;
    else
    {
    flag[d]++;
    preemption_count++;
    }
    }
    printf("\nPreemption Count = %d",preemption_count);
    }
    return 0;
    
    /*(LLF) Scheduling*/
    float timeremaining[20];
    printf("\nLeast Laxity First Algorithm\n");
    fscanf(read,"%d ",&n);
    for(i=0;i<n;i++)
    {
    fscanf(read,"%f ",&release[i]);
    fscanf(read,"%f ",&period[i]);
    fscanf(read,"%f ",&exec[i]);
    fscanf(read,"%f ",&deadline[i]);
    }
    fclose(read);
    for(i=0;i<n;i++)
    {
    individual_util[i]=exec[i]/period[i];
    util+=individual_util[i];
    deadline[i]=period[i];
    instance[i]=0.0f;
    timeremaining[i]=exec[i];
    }
    util=util*100;
    if(util>100)
    printf("\nUtilisation factor = %0.2f",util);
    else
    {
    printf("\nUtilisation factor = %0.2f",util);
    printf("\nHyperperiod of the given task set is : %d\n\n" ,k=hyperperiod(period , n));
    c=0;
    while(time<k)
    {
    nexttime=time+1;
    task = llf(period,n,time,deadline,timeremaining);
    if(task==-1)
    {printf("-");time++;continue;}

    instance[task]++;
    printf("T%d",task);
    ex[c++]=task;
    if(instance[task]==exec[task])
    {
    timeremaining[task]=exec[task];
    tempmax=nexttime-(period[task]-deadline[task]);
    if(instance[task]<tempmax)
    {
    responsemax[task]=tempmax;
    }
    else
    {responsemin[task]=instance[task];
    }

    if(deadline[task]==k)
    {responsemin[task]=responsemax[task];
    }

    period[task]+=deadline[task];
    instance[task]=0.0f;

    }
    time++;
    }
    for(i=0;i<n;i++)
    {
    printf("Maximum Response time of Task %d = %f",i,responsemax[i]);
    printf("\nMinimum Response time of Task %d = %f",i,responsemin[i]);
    }
    preemption_count=0;
    for(i=0;i<k;i=j)
    {
    flag[i]=1;
    d=ex[i];
    for(j=i+1;d==ex[j];j++)
    flag[d]++;
    if(flag[d]==exec[d])
    flag[d]=1;
    else
    {
    flag[d]++;
    preemption_count++;

    }
    }
    printf("\nPreemption Count = %d",preemption_count);
    }
    return 0;
    }



