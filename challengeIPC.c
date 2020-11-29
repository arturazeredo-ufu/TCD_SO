#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#define gettid() syscall(SYS_gettid)
#define QUEUE_SZ 10
#define PIDS_SZ  8

struct queue_t { 
	sem_t mutex;
	int fst, lst, count; 
	int array[QUEUE_SZ];
};
typedef struct queue_t * Queue;

#define F1_SM_SZ   sizeof(struct queue_t)
#define PIDS_SM_SZ PIDS_SZ*sizeof(int)

Queue F1;
int* pids;
long int thread1p4Id;

void* consumerF1(); 
int   createChildren();
void  createF1 (int keySM);
void  createPids (int keySM);
void  createSemaphore (sem_t * semaphore);
void  initQueue (Queue queue);
int   isEmpty (Queue queue);
int   isFull (Queue queue);
int   next (int position);
int   pop (Queue queue, int * value);
void  producerF1();
int   push (Queue queue, int value);

int main () {
	srand(time(NULL));
	createF1(rand());
	initQueue(F1);
	createPids(rand());
	*pids = getpid();

	int id = createChildren();

	if ( id <= 3 ){
		signal(SIGUSR2, producerF1);
		pause();
	} else if ( id == 4 ){
		thread1p4Id = gettid();
		pthread_t thread2;
		pthread_create(&thread2, NULL, consumerF1, NULL);
		consumerF1();
		pthread_join(thread2, NULL);
	} else if ( id == 5 ){

	} else if ( id == 6 ){

	} else if ( id == 7 ){

	}

	return 0;
}

void initQueue (Queue queue) {
	queue->fst   = 0;
	queue->lst   = 0;
	queue->count = 0;
	createSemaphore(&queue->mutex);
	push(F1, 1);
}

int isFull (Queue queue) {
	return queue->count == QUEUE_SZ;
}

int isEmpty (Queue queue) {
	return queue->count == 0;
}

int next (int position) {
	return (position + 1) % QUEUE_SZ;
}

int push (Queue queue, int value) {
	sem_wait((sem_t*)&queue->mutex);
	if (isFull(queue)) {
		sem_post((sem_t*)&queue->mutex);	
		return -1;
	}
	printf("%d insere %d\n", getpid(), value);

	queue->array[queue->lst] = value;
	queue->lst = next(queue->lst);
	queue->count++;
	int flagSendSignal = isFull(queue);
	sem_post((sem_t*)&queue->mutex);

	return flagSendSignal;
}

int pop (Queue queue, int * value) {
	sem_wait((sem_t*)&queue->mutex);
	if (isEmpty(queue)) {
		sem_post((sem_t*)&queue->mutex);
		return -1;
	}
	*value = queue->array[queue->fst];
	queue->fst = next(queue->fst);
	queue->count--;
	
	printf("%d remove %d\n", getpid(), *value);
	int flagSendSignal = isEmpty(queue);
	sem_post((sem_t*)&queue->mutex);

	return flagSendSignal;	
}

void createF1 (int keySM) {
	key_t key=keySM;
	void *shared_memory = (void *)0;
	int shmid;

	shmid = shmget(key, F1_SM_SZ, 0666|IPC_CREAT);
	if ( shmid == -1 ) {
		printf("shmget failed\n");
		exit(-1);
	}

	shared_memory = shmat(shmid,(void*)0,0);
  
	if (shared_memory == (void *) -1 ) {
		printf("shmat failed\n");
		exit(-1);
  	}

	F1 = (Queue) shared_memory;	
}

void createPids (int keySM) {
	key_t key=keySM;
	void *shared_memory = (void *)0;
	int shmid;

	shmid = shmget(key, PIDS_SM_SZ, 0666|IPC_CREAT);
	if ( shmid == -1 ) {
		printf("shmget failed\n");
		exit(-1);
	}

	shared_memory = shmat(shmid,(void*)0,0);
  
	if (shared_memory == (void *) -1 ) {
		printf("shmat failed\n");
		exit(-1);
  	}

	pids = (int *) shared_memory;	
}

void createSemaphore (sem_t * semaphore) {
	if ( sem_init(semaphore,1,1) != 0 ) {
		printf("Semaphore creation failed\n");
		exit(-1);
	}
}


int createChildren() {
	pid_t p;
	int id;
	for(id=1; id<=7; id++){
		p = fork();
		if ( p < 0 ) {
			printf("fork failed\n");
			exit(-1);
		}
		if ( p == 0 ) {
			printf("%d\t%d\n", getpid(), id);
			break;
		}
		*(pids+id) = p;
	}
	
	if(p > 0)
		for (int i = 0; i < 8; i++) 
			wait(NULL);
		
	return id;
}

void producerF1() {
	int response, random;
	srand(getpid() + F1->lst);
	while(1) {
		random = rand()%1000;
		response = push(F1, random);
		if(response == 1) {
			for (int i = 0; i < 2; ++i) {
				sleep(0.5);
				while(kill(*(pids+4), SIGUSR1) == -1);
			}
			break;
		} else if (response == -1) 
			break;
	}
}

void* consumerF1() {
	int response, value;
	while(1) {
		response = pop(F1, &value);
		if(response == 1) {
			if (gettid() == thread1p4Id){
				for (int i = 1; i < 4; ++i) {
					sleep(0.5);
					while(kill(*(pids+i), SIGUSR2) == -1);
				}
			}
			signal(SIGUSR1, (__sighandler_t) consumerF1);
			pause();
			break;
		} else if (response == 0) {
			printf("insere na pipe\n");
		} else if (response == -1) 
			break;
	}
}