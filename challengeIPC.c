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
int pipe01[2];
int pipe02[2];

void* consumerF1(); 
void  controlPipe(int pipe);
int   createChildren();
void  createF1 (int keySM);
void  createPids (int keySM);
void  createPipes();
void  createSemaphore (sem_t * semaphore);
void  initQueue (Queue queue);
int   isEmpty (Queue queue);
int   isFull (Queue queue);
int   next (int position);
int   pop (Queue queue, int * value);
void  producerF1();
int   push (Queue queue, int value);
void* sigHandlerConsumerF1();
void* sigHandlerProducerF1();

int main () {
	srand(time(NULL));
	createF1(rand());
	initQueue(F1);
	createPids(rand());
	createPipes();
	*pids = getpid();

	int id = createChildren();

	if ( id <= 3 ){
		sigHandlerProducerF1();
	} else if ( id == 4 ){
		thread1p4Id = gettid();
		pthread_t thread2;
		pthread_create(&thread2, NULL, sigHandlerConsumerF1, NULL);
		sigHandlerConsumerF1();
		pthread_join(thread2, NULL);
	} else if ( id == 5 ){
		controlPipe(1);
	} else if ( id == 6 ){
		controlPipe(2);
	} else if ( id == 7 ){

	}

	return 0;
}

void initQueue (Queue queue) {
	queue->fst   = 0;
	queue->lst   = 0;
	queue->count = 0;
	createSemaphore(&queue->mutex);
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
	// printf("%d insere %d\n", getpid(), value);

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
	
	// printf("%d remove %d\n", getpid(), *value);
	
	sem_post((sem_t*)&queue->mutex);
	return 0;	
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

void createPipes() {
	if ( pipe(pipe01) == -1 ){ printf("Erro pipe()"); exit(-1); }
	if ( pipe(pipe02) == -1 ){ printf("Erro pipe()"); exit(-1); }
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
			// printf("%d\t%d\n", getpid(), id);
			break;
		}
		*(pids+id) = p;
	}
	
	if(p > 0)
		for (int i = 0; i < 8; i++) 
			wait(NULL);
		
	return id;
}

void* sigHandlerProducerF1() {
	signal(SIGUSR2, producerF1);
	pause();
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

void* sigHandlerConsumerF1() {
	if (gettid() == thread1p4Id) {
		for (int i = 1; i < 4; ++i) {
			sleep(0.5);
			while(kill(*(pids+i), SIGUSR2) == -1);
		}
	}
	signal(SIGUSR1, (__sighandler_t) consumerF1);
	pause();
}

void* consumerF1() {
	int response, value;
	while(1) {
		response = pop(F1, &value);
		if (response == 0) {
			
			if (thread1p4Id == gettid())
				write(pipe01[1], &value, sizeof(int));	
			else 
				write(pipe02[1], &value, sizeof(int));

			// printf("%ld insere na pipe %d\n", gettid(), value);
		} else if (response == -1) 
			break;
	}
}

void controlPipe(int pipe) {
	int valor, resp;
	while(1) {
		
		if (pipe == 1) {
			resp = read(pipe01[0], &valor, sizeof(int));
		} else if (pipe == 2) {
			resp = read(pipe02[0], &valor, sizeof(int));
		}

		if(resp == -1) {
			printf("Erro na leitura do pipe0%d\n", pipe);
		} else if (resp > 0) {
			
			printf("pipe0%d insere %d na Fila2\n", pipe, valor);
		}
	}
}
