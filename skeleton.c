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
#define F2_SZ    48
#define QUEUE_SZ 10

struct fila1{ 
	int pos; // pos == 40: fila vazia | pos == 20: fila cheia
	sem_t mutex;
	sem_t sync;
	int pids[8];
	int queue[QUEUE_SZ];
};

struct fila2{ 
	int contador;
	int pos;
	int queue[QUEUE_SZ];
};

struct fila1 *fila1_ptr;
struct fila2 *fila2_ptr;
int pipe01[2];
int pipe02[2];
long int thread1p4Id;

void  controlaPipe(int pipe);
void  criaFila(int fila, int keySM);
int   criaFilhos();
void  criaPipes();
void  criaSemaforos();
void  inicializaFilas();
void  p1p2p3Produtor();
void  p4Consumidor();
void* p4ControlaSignal();
// void printaFila(void);


int main(){
	srand(time(NULL));
	criaFila(1, rand());
	criaFila(2, rand());
	inicializaFilas();
	criaSemaforos();
	criaPipes();

	int id = criaFilhos();	

	if ( id > 0 && id <= 3 ){
		signal(SIGUSR2, p1p2p3Produtor);
		pause();
	} else if ( id == 4 ){
		thread1p4Id = gettid();
		pthread_t thread2;
		pthread_create(&thread2, NULL, p4ControlaSignal, NULL);
		p4ControlaSignal();
		pthread_join(thread2, NULL);
	} else if ( id == 5 ){
		controlaPipe(1);
	} else if ( id == 6 ){
		controlaPipe(2);
	} else if ( id == 7 ){
	}

	close(pipe01[0]);
	close(pipe01[1]);
	close(pipe02[0]);
	close(pipe02[1]);

	exit(0); 
}


void criaFila(int fila, int keySM) {
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

  	if (fila == 1) {
  		fila1_ptr = (struct fila1 *) shared_memory;	
  	} else if (fila == 2) {
  		fila2_ptr = (struct fila2 *) shared_memory;
  	}
}


void  inicializaFilas() {
	int i;
	for (i = 0; i < QUEUE_SZ; i++)
		fila1_ptr->queue[i] = 0;
	for (i = 0; i < QUEUE_SZ; i++)
		fila2_ptr->queue[i] = 0;
	fila1_ptr->pos=40;
}


void criaSemaforos() {
	if ( sem_init((sem_t *)&fila1_ptr->mutex,1,1) != 0 ) {printf("mutex falhou\n");exit(-1);}
	if ( sem_init((sem_t *)&fila1_ptr->sync,1,1) != 0 )  {printf("sync falhou\n" );exit(-1);}
}


void criaPipes() {
	if ( pipe(pipe01) == -1 ){ printf("Erro pipe()"); exit(-1); }
	if ( pipe(pipe02) == -1 ){ printf("Erro pipe()"); exit(-1); }
}


int criaFilhos() {
	// printf("Criacao dos processos filhos:\n");
	pid_t p;
	int id;
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
	srand(getpid() + fila1_ptr->pos);
	sem_wait((sem_t*)&fila1_ptr->mutex);
	
	if (fila1_ptr->pos == 40) {
		fila1_ptr->pos = 0;
		sem_post((sem_t*)&fila1_ptr->mutex);
		p1p2p3Produtor();
	} else if (fila1_ptr->pos < 9) {

		fila1_ptr->queue[fila1_ptr->pos] = rand()%1000;
		printf("%d insere %d na posicao %d\n", getpid(), fila1_ptr->queue[fila1_ptr->pos], fila1_ptr->pos);
		fila1_ptr->pos++;
		
		sem_post((sem_t*)&fila1_ptr->mutex);
		p1p2p3Produtor();
	
	} else if (fila1_ptr->pos == 9) {
	
		fila1_ptr->queue[fila1_ptr->pos] = rand()%1000;
		printf("%d insere %d na posicao %d\n", getpid(), fila1_ptr->queue[fila1_ptr->pos], fila1_ptr->pos);
		fila1_ptr->pos = 20;
	
		sleep(0.5);
		while(kill(fila1_ptr->pids[4], SIGUSR1) == -1);
		sem_post((sem_t*)&fila1_ptr->mutex);
	
	} else {
		sem_post((sem_t*)&fila1_ptr->mutex);
	}
}


void* p4ControlaSignal(void) {
	if (thread1p4Id == gettid()) {
		if (fila1_ptr->pos == 40) {
			for (int i=1; i<4; i++){
				sleep(0.5);
				while(kill(fila1_ptr->pids[i], SIGUSR2) == -1);
			}
		}
	}
		
	signal(SIGUSR1, p4Consumidor);
	pause();
}


void p4Consumidor() {
	int pipeId;
	sem_wait((sem_t*)&fila1_ptr->mutex);
	if (fila1_ptr->pos == 20) {
		fila1_ptr->pos = 0;
		sem_post((sem_t*)&fila1_ptr->mutex);
		p4Consumidor();
	} else if (fila1_ptr->pos < 9) {
		if (thread1p4Id == gettid()){
			write(pipe01[1], &fila1_ptr->queue[fila1_ptr->pos], sizeof(int));	
			pipeId = 1;
		} else {
			write(pipe02[1], &fila1_ptr->queue[fila1_ptr->pos], sizeof(int));
			pipeId = 2;
		}
		// printf("thread %ld escreve %d na pipe0%d\n", gettid(), fila1_ptr->queue[fila1_ptr->pos], pipeId);
		fila1_ptr->pos++;
		
		sem_post((sem_t*)&fila1_ptr->mutex);
		p4Consumidor();
	
	} else if (fila1_ptr->pos == 9) {
	
		if (thread1p4Id == gettid()){
			write(pipe01[1], &fila1_ptr->queue[fila1_ptr->pos], sizeof(int));	
			pipeId = 1;
		} else {
			write(pipe02[1], &fila1_ptr->queue[fila1_ptr->pos], sizeof(int));
			pipeId = 2;
		}
		// printf("thread %ld escreve %d na pipe0%d\n", gettid(), fila1_ptr->queue[fila1_ptr->pos], pipeId);
		fila1_ptr->pos = 40;
	
		sem_post((sem_t*)&fila1_ptr->mutex);
	
	} else {
		sem_post((sem_t*)&fila1_ptr->mutex);
	}
}

void controlaPipe(int pipe) {
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
			// printf("Pipe%d insere %d na Fila2\n", pipe, valor);
		}
	}
}

// void printaFila(void) {
// 	int i;
// 	printf("\nF1:\n[");
// 	for (i=0;i<QUEUE_SZ;++i) {
// 		printf("%d", fila1_ptr->queue[i]);
// 		if(i != QUEUE_SZ-1)
// 			printf(", ");	
// 	}
// 	printf("]\n\n");
// }
