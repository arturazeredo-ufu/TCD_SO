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
#define MEM_SZ   140
#define QUEUE_SZ 10

struct shared_area{ 
	int num;
	sem_t mutex;
	sem_t sync;
	int pids[8];
	int queue[QUEUE_SZ];
};

struct shared_area *shared_area_ptr;
int pipe01[2];
int pipe02[2];

int    criaFilhos();
struct shared_area * criaFila1();
void   inicializarFila1();
void   p1p2p3Produtor(int id);
void*  p4Consumidor(void * thread1Id);
void   p4CriaThread();
void   teste();

void printaFila(void) {
	int i;
	for (i=0;i<10;++i) {
		printf("%d ", shared_area_ptr->queue[i]	);	
	}
	printf("\n\n");
}

int main(){
	criaFila1(9827);
	
	if ( sem_init((sem_t *)&shared_area_ptr->mutex,1,1) != 0 ) {printf("mutex falhou\n");exit(-1);}
	if ( sem_init((sem_t *)&shared_area_ptr->sync,1,1) != 0 )  {printf("sync falhou\n" );exit(-1);}

	if ( pipe(pipe01) == -1 ){ printf("Erro pipe()"); return -1; }
	if ( pipe(pipe02) == -1 ){ printf("Erro pipe()"); return -1; }

	int id = criaFilhos();	

	if ( id <= 3 ){
		if (id == 1) {
			signal(SIGUSR2, p1p2p3Produtor);
			p1p2p3Produtor(id);
			pause();
		} else {
			p1p2p3Produtor(id);
		}
	}
	else if ( id == 4 ){
		signal(SIGUSR1, p4CriaThread);
		pause();
	}
	else if ( id == 5 ){
		// int y;
		// while(pipe01[0] != 0) {
		// 	read(pipe01[0], &y, sizeof(int));
		// 	printf("p5 leu %d\n", y);
		// }
	}
	else if ( id == 6 ){
		// int y;
		// while(pipe02[0] != 0) {
		// 	read(pipe02[0], &y, sizeof(int));
		// 	printf("p6 leu %d\n", y);
		// }
		
	}
	else if ( id == 7 ){
	}

	close(pipe01[0]);
	close(pipe01[1]);
	close(pipe02[0]);
	close(pipe02[1]);

	printf("Sai %d\n", getpid());

	exit(0); 
}

struct shared_area * criaFila1(int keySM) {
	key_t key=keySM;
	void *shared_memory = (void *)0;
	int shmid;

	shmid = shmget(key,MEM_SZ,0666|IPC_CREAT);
	if ( shmid == -1 ) {
		printf("shmget falhou\n");
		exit(-1);
	}

	shared_memory = shmat(shmid,(void*)0,0);
  
	if (shared_memory == (void *) -1 ) {
		printf("shmat falhou\n");
		exit(-1);
  	}
	
	shared_area_ptr = (struct shared_area *) shared_memory;

	inicializarFila1();
}

void  inicializarFila1() {
	shared_area_ptr->pids[0] = getpid();
	int i;
	for (i = 0; i < MEM_SZ; i++)
		shared_area_ptr->queue[i] = 0;
	for (i = 0; i < 8; i++)
		shared_area_ptr->pids[i] = 0; 
	shared_area_ptr->num=0;
}

int criaFilhos() {
	pid_t p;
	int id=0;
	sem_wait((sem_t*)&shared_area_ptr->sync);
	for(id=1; id<=7; id++){
		p = fork();
		if ( p < 0 ) {
			printf("Erro no fork()\n");
			exit(-1);
		}
		if ( p == 0 ){
			sem_wait((sem_t*)&shared_area_ptr->sync);
			break;
		}
		shared_area_ptr->pids[id] = p; 
		printf("id: %d\tpid: %d\n", id, shared_area_ptr->pids[id]);
	}

	if(p > 0) {
		for (int i = 0; i < 8; ++i) 
			sem_post((sem_t*)&shared_area_ptr->sync);
		for (int i = 0; i < 7; i++) 
			wait(NULL);
	} 
		
	return id;
}

void p1p2p3Produtor(int id) {
	srand(getpid());
	while (shared_area_ptr->num <= 9) {
		sem_wait((sem_t*)&shared_area_ptr->mutex);
		if (shared_area_ptr->num <= 9) {
			shared_area_ptr->queue[shared_area_ptr->num] = rand()%1000;
			shared_area_ptr->num++;
			if (shared_area_ptr->num == 9) {
				sleep(000.5);
				while(kill(shared_area_ptr->pids[4], SIGUSR1) == -1);
				sem_post((sem_t*)&shared_area_ptr->mutex);
				break;
			}
			sem_post((sem_t*)&shared_area_ptr->mutex);
		} else {
			sem_post((sem_t*)&shared_area_ptr->mutex);
			break; 
		}
	}
}

void p4CriaThread() {
	shared_area_ptr->num = 0;
	int thread1id = gettid();

	pthread_t thread2;
	pthread_create(&thread2, NULL, p4Consumidor, &thread1id);
	p4Consumidor(&thread1id);
	pthread_join(thread2, NULL);
	
	while(kill(shared_area_ptr->pids[1], SIGUSR2) == -1);
}

void* p4Consumidor(void * thread1IdPointer) {
	int *thread1Id = (int *)thread1IdPointer;
	
	while (shared_area_ptr->num <= 9) {
	
		sem_wait((sem_t*)&shared_area_ptr->mutex);
	
		if (shared_area_ptr->num <= 9) {

			if (gettid() == *thread1Id) {
				write(pipe01[1], &shared_area_ptr->queue[shared_area_ptr->num], sizeof(int));
			} else {
				write(pipe02[1], &shared_area_ptr->queue[shared_area_ptr->num], sizeof(int));
			}
			
			shared_area_ptr->queue[shared_area_ptr->num] = 0;
			shared_area_ptr->num++;
			
			sem_post((sem_t*)&shared_area_ptr->mutex);
			if (shared_area_ptr->num == 9)
				break;	
		} else {
			sem_post((sem_t*)&shared_area_ptr->mutex);
			break; 
		}
	}
}
