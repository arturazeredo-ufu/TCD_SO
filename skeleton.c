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
#define F1_SZ    140
#define F2_SZ    44
#define QUEUE_SZ 10

struct fila1{ 
	int num;
	sem_t mutex;
	sem_t sync;
	int pids[8];
	int queue[QUEUE_SZ];
};

struct fila2{ 
	int num;
	int queue[QUEUE_SZ];
};

struct fila1 *fila1_ptr;
struct fila2 *fila2_ptr;
int pipe01[2];
int pipe02[2];

int    criaFilhos();
struct fila1 * criaFila1();
struct fila2 * criaFila2();
void   inicializarFila1();
void   p1p2p3Produtor();
void*  p4Consumidor(void * thread1Id);
void   p4CriaThread();
void   teste();

void printaFila(void) {
	int i;
	printf("\nF1:\n[");
	for (i=0;i<QUEUE_SZ;++i) {
		printf("%d", fila1_ptr->queue[i]);
		if(i != QUEUE_SZ-1)
			printf(", ");	
	}
	printf("]\n");
}

int main(){
	criaFila1(9827);
	criaFila2(1345);
	
	if ( sem_init((sem_t *)&fila1_ptr->mutex,1,1) != 0 ) {printf("mutex falhou\n");exit(-1);}
	if ( sem_init((sem_t *)&fila1_ptr->sync,1,1) != 0 )  {printf("sync falhou\n" );exit(-1);}

	if ( pipe(pipe01) == -1 ){ printf("Erro pipe()"); return -1; }
	if ( pipe(pipe02) == -1 ){ printf("Erro pipe()"); return -1; }

	int id = criaFilhos();	

	if ( id <= 3 ){
		signal(SIGUSR2, p1p2p3Produtor);
		pause();
	}
	else if ( id == 4 ){
		if (fila1_ptr->num == 0) 
			for (int i=1; i<4; i++){
				sleep(0.5);
				while(kill(fila1_ptr->pids[i], SIGUSR2) == -1);
			}
		signal(SIGUSR1, p4CriaThread);
		pause();
	}
	else if ( id == 5 ){
		// read_pipe(id);
	}
	else if ( id == 6 ){
		// read_pipe(id);
	}
	else if ( id == 7 ){
	}

	close(pipe01[0]);
	close(pipe01[1]);
	close(pipe02[0]);
	close(pipe02[1]);

	exit(0); 
}

struct fila1 * criaFila1(int keySM) {
	key_t key=keySM;
	void *shared_memory = (void *)0;
	int shmid;

	shmid = shmget(key,F1_SZ,0666|IPC_CREAT);
	if ( shmid == -1 ) {
		printf("shmget falhou\n");
		exit(-1);
	}

	shared_memory = shmat(shmid,(void*)0,0);
  
	if (shared_memory == (void *) -1 ) {
		printf("shmat falhou\n");
		exit(-1);
  	}
	
	fila1_ptr = (struct fila1 *) shared_memory;

	inicializarFila1();
}

void  inicializarFila1() {
	fila1_ptr->pids[0] = getpid();
	int i;
	for (i = 0; i < F1_SZ; i++)
		fila1_ptr->queue[i] = 0;
	for (i = 0; i < 8; i++)
		fila1_ptr->pids[i] = 0; 
	fila1_ptr->num=0;
}

struct fila2 * criaFila2(int keySM) {
	key_t key=keySM;
	void *shared_memory = (void *)0;
	int shmid;

	shmid = shmget(key,F2_SZ,0666|IPC_CREAT);
	if ( shmid == -1 ) {
		printf("shmget falhou\n");
		exit(-1);
	}

	shared_memory = shmat(shmid,(void*)0,0);
  
	if (shared_memory == (void *) -1 ) {
		printf("shmat falhou\n");
		exit(-1);
  	}
	
	fila1_ptr = (struct fila1 *) shared_memory;

	inicializarFila1();
}

int criaFilhos() {
	printf("Criacao dos processos filhos:\n");
	pid_t p;
	int id=0;
	sem_wait((sem_t*)&fila1_ptr->sync);
	for(id=1; id<=7; id++){
		p = fork();
		if ( p < 0 ) {
			printf("Erro no fork()\n");
			exit(-1);
		}
		if ( p == 0 ){
			sem_wait((sem_t*)&fila1_ptr->sync);
			break;
		}
		fila1_ptr->pids[id] = p; 
		printf("id: %d\tpid: %d\n", id, fila1_ptr->pids[id]);
	}

	if(p > 0) {
		printf("\n");
		for (int i = 0; i < 8; ++i) 
			sem_post((sem_t*)&fila1_ptr->sync);
		for (int i = 0; i < 7; i++) 
			wait(NULL);
	} 
		
	return id;
}

void p1p2p3Produtor() {
	srand(getpid() + fila1_ptr->num);
	sem_wait((sem_t*)&fila1_ptr->mutex);
	if (fila1_ptr->num < 9) {

		fila1_ptr->queue[fila1_ptr->num] = rand()%1000;
		printf("%d insere %d na posicao %d\n", getpid(), fila1_ptr->queue[fila1_ptr->num], fila1_ptr->num);
		fila1_ptr->num++;
		
		sem_post((sem_t*)&fila1_ptr->mutex);
		p1p2p3Produtor();
	
	} else if (fila1_ptr->num == 9) {
	
		fila1_ptr->queue[fila1_ptr->num] = rand()%1000;
		printf("%d insere %d na posicao %d\n", getpid(), fila1_ptr->queue[fila1_ptr->num], fila1_ptr->num);
		fila1_ptr->num++;
	
		sleep(0.5);
		while(kill(fila1_ptr->pids[4], SIGUSR1) == -1);
		sem_post((sem_t*)&fila1_ptr->mutex);
	
	} else {
		sem_post((sem_t*)&fila1_ptr->mutex);
	}
}

void p4CriaThread() {
	printaFila();
	fila1_ptr->num = 0;

	int thread1id = gettid();

	pthread_t thread2;
	pthread_create(&thread2, NULL, p4Consumidor, &thread1id);
	p4Consumidor(&thread1id);
	pthread_join(thread2, NULL);
	fila1_ptr->num = 0;
}

void* p4Consumidor(void * thread1IdPointer) {
	int *thread1Id = (int *)thread1IdPointer;
	int pipeId;
	while (fila1_ptr->num <= 9) {
	
		sem_wait((sem_t*)&fila1_ptr->mutex);
	
		if (fila1_ptr->num <= 9) {
			if (gettid() == *thread1Id){
				pipeId = 1;
				write(pipe01[1], &fila1_ptr->queue[fila1_ptr->num], sizeof(int));
			}
			else {
				pipeId = 2;
				write(pipe02[1], &fila1_ptr->queue[fila1_ptr->num], sizeof(int));
			}
			printf("thread %ld escreve %d na pipe0%d\n", gettid(), fila1_ptr->queue[fila1_ptr->num], pipeId);
			
			fila1_ptr->queue[fila1_ptr->num] = 0;
			fila1_ptr->num++;
			
			sem_post((sem_t*)&fila1_ptr->mutex);
			if (fila1_ptr->num == 9)
				break;	
		} else {
			sem_post((sem_t*)&fila1_ptr->mutex);
			break; 
		}
	}
}

