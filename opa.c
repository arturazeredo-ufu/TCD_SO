#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#define gettid() syscall(SYS_gettid)
#define QUEUE_SZ 10 //Tamanho das filas (F1 e F2)
#define PIDS_SZ  8  //Tamanho do vetor com PIDs de todos os processos
#define AMOUNT_DATA 10000 //Quantidade de elementos que devem ser processados por p7
#define INTERVAL 1000

//Estrutura Fila 1
struct queue1_t { 
	sem_t mutex;
	int fst, lst, count; 
	int array[QUEUE_SZ];
	int toggleAction; // 0: Produzir  |  1: Consumir
	int sendSignal;   // 0: P4 não pode receber sinal  |  1: P4 pode receber sinal 
};
typedef struct queue1_t * Queue1;

#define SM_QUEUE1_SZ sizeof(struct queue1_t) 
#define SM_PIDS_SZ   PIDS_SZ*sizeof(int)
#define SM_SYNC      sizeof(sem_t)

Queue1 F1; //Ponteiro para estrutura da fila 1 (shared memory)
int* pids; //Vetor com PIDs de todos os processos [pai,p1,p2,p3,p4,p5,p6,p7] (shared memory)
sem_t* syncChildren; //Ponteiro para semáforo para sincronização da criação dos filhos (shared memory)
long int thread1p4Id; //TID da thread original do P4

void  consumerF1();
int   createChildren();
void  createSharedMemory (int type, int sharedMemorySize, int keySM);
void  createSemaphore (sem_t * semaphore);
int   isEmptyF1 (Queue1 queue);
int   isFullF1 (Queue1 queue);
int   next (int position);
void* p4SignalReceiver();
int   popF1 (int * value);
void  producerF1(); 
int   pushF1 (int value);
void* setF1ToConsume();
void  setF1ToProduce();

int main() {

	//Criação e inicialização das Shared Memories
	srand(time(NULL));
	createSharedMemory(1, SM_QUEUE1_SZ, random()); //F1
	createSharedMemory(2, SM_PIDS_SZ,   random()); //pids
	createSharedMemory(3, SM_SYNC,      random()); //syncChildren

	//Cria processos filhos
	int id = createChildren();

	//Processo pai
	if ( id == 0 ) {
		// clock_t end = clock();
		// printResult((double)(end - begin) / CLOCKS_PER_SEC);
	}	

	//P1, P2, P3
	else if ( id <= 3 ){
		while(1) {
			while(F1->toggleAction != 0);
			producerF1();
		}
	}

	//P4
	else if ( id == 4 ){
		thread1p4Id = gettid(); //Defino TID da thread principal do p4
		
		pthread_t thread2;
		pthread_create(&thread2, NULL, p4SignalReceiver, NULL);
		p4SignalReceiver();
		pthread_join(thread2, NULL);
	} 


	return 0;
}

//Argumento type: 
//	1 = Ponteiro para F1
//  2 = Ponteiro para vetor de PIDs
//  3 = Semáforo de sincronização de filhos
void createSharedMemory (int type, int sharedMemorySize, int keySM) {
	key_t key = keySM;
	void *sharedMemory = (void *)0;
	int shmid;

	shmid = shmget(key, sharedMemorySize, 0666|IPC_CREAT);
	if ( shmid == -1 ) {
		printf("shmget failed\n");
		exit(-1);
	}

	sharedMemory = shmat(shmid,(void*)0,0);
  
	if (sharedMemory == (void *) -1 ) {
		printf("shmat failed\n");
		exit(-1);
  	}

  	if (type == 1) {
  		F1 = (Queue1) sharedMemory;
  		createSemaphore(&F1->mutex);
  	} else if (type == 2) {
  		pids = (int *) sharedMemory;
  		*(pids) = getpid(); // pids[0] = Pid do processo pai
  	} else if (type == 3) {
  		syncChildren = (sem_t *) sharedMemory;
  		createSemaphore(syncChildren);
  	}
}

//Inicializa semáforo
void createSemaphore (sem_t * semaphore) {
	if ( sem_init(semaphore,1,1) != 0 ) {
		printf("Semaphore creation failed\n");
		exit(-1);
	}
}

//Processo pai cria todos os 7 filhos
int createChildren() {
	pid_t p;
	int id;

	sem_wait((sem_t*)&syncChildren); //Pai fecha semáforo
	for(id=1; id<=7; id++){
		p = fork();
		
		if ( p < 0 ) {
			printf("fork failed\n");
			exit(-1);
		}
		
		if ( p == 0 ) {
			sem_wait((sem_t*)&syncChildren); //Filhos aguardam liberação do pai
			// printf("p%d  --> %d\n", id, getpid());
			return id;
		}

		*(pids+id) = p; //Pai recebe PID do filho criado e insere valor no vetor pids
	}

	for (int i = 0; i < 8; ++i)
		sem_post((sem_t*)&syncChildren); //Pai libera todos os filhos

	for (int i = 0; i < 7; i++)
		wait(NULL); //Pai espera o p7 encerrar todos os filhos
	

	return 0;
}


//p1, p2 e p3 produzem elementos aleatórios para F1
void producerF1() {
	int response, random;
	srand(getpid() + F1->lst); //Seed para função random() sempre muda dessa forma
	while(1) {

		sem_wait((sem_t*)&F1->mutex);
		if (F1->toggleAction == 1){
			sem_post((sem_t*)&F1->mutex);	
			break; //Se a fila já estiver sendo consumida, não posso produzir
		} 
		sem_post((sem_t*)&F1->mutex);

		random = (rand()%INTERVAL)+1; //Gera número aleatório entre 1 e 1000
		response = pushF1(random); //Tenta inserir na F1

		if(response == 1) { //Último elemento inserido na fila
			while(F1->sendSignal != 1); // Espero p4 estar pronto para receber sinal
			while(kill(*(pids+4), SIGUSR1) == -1); //Tento enviar sinal para p4 consumir até ter sucesso
			break;

		} else if (response == -1) //Fila cheia
			break;
	}
}

//Tenta inserir elemento "value" na fila "queue"
int pushF1 (int value) {
	sem_wait((sem_t*)&F1->mutex);
	
	if (isFullF1(F1)) {
		sem_post((sem_t*)&F1->mutex);	
		return -1;
	}

	F1->array[F1->lst] = value; 
	printf("%d insere %d na F1 na posicao: %d\n", getpid(), value, F1->count);	
	F1->lst = next(F1->lst);
	F1->count++;

	//Caso inserção encheu a fila, flagSendSignal == 1
	int flagSendSignal = isFullF1(F1); 
	
	sem_post((sem_t*)&F1->mutex);
	return flagSendSignal;
}

//Verifica se fila está cheia
int isFullF1 (Queue1 queue) {
	return queue->count == QUEUE_SZ;
}

//Calcula próxima posição livre para realizar a inserção 
int next (int position) {
	return (position + 1) % QUEUE_SZ; //Inserção circular
}

//Threads de p4 aguardam envio do sinal dos produtores
void* p4SignalReceiver() {
	while(1) {
		if (gettid() == thread1p4Id) {
			signal(SIGUSR1, (__sighandler_t) setF1ToConsume);
			F1->sendSignal = 1; //Pronto para receber signal
		}

		while(F1->toggleAction != 1); //Enquanto a fila não estiver pronta para consumo, não fazer nada

		consumerF1();
		setF1ToProduce();
	}
}

//Bloqueia produtores de continuarem produzindo ao retirar elementos da F1
void* setF1ToConsume() {
	F1->toggleAction = 1; //Não produza mais! F1 pode ser consumida
}

//p4 (dualThread) consome F1 e escreve elementos nas pipes
void consumerF1() {
	int response, value;
	while(1) {
		response = popF1(&value);
		if (response == 0) {
			
			if (thread1p4Id == gettid()){
				// printf("%ld mandou %d pra pipe01\n", gettid(), value);
				// write(pipe01[1], &value, sizeof(int));
			}
			else {
				// printf("%ld mandou %d pra pipe02\n", gettid(), value);
				// write(pipe02[1], &value, sizeof(int));
			}

		} else if (response == -1) 
			break;
	}
}

//Tenta retirar um elemento da fila 1 e inserir em "value" passado por referência
int popF1 (int * value) {
	sem_wait((sem_t*)&F1->mutex);

	if (isEmptyF1(F1)) {
		sem_post((sem_t*)&F1->mutex);
		return -1;
	}

	*value = F1->array[F1->fst]; 

	printf("%d remove %d da F1\n", getpid(), *value);	
	
	F1->fst = next(F1->fst);
	F1->count--;

	sem_post((sem_t*)&F1->mutex);
	return 0;
}

//Verifica se fila está vazia
int isEmptyF1 (Queue1 queue) {
	return queue->count == 0;
}

//Libera produtores para produzir elementos para F1
void setF1ToProduce() {
	F1->toggleAction = 0; //Produza mais! F1 pode não pode ser consumida
}
