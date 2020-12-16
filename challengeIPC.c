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

//Estrutura das filas 1 e 2
struct queue_t { 
	sem_t mutex;
	int fst, lst, count; 
	int array[QUEUE_SZ];
};
typedef struct queue_t * Queue;

//Estrutura das flags utilizadas para controle de produção e consumo das filas
struct flag {
	sem_t mutex;
	int flag; // 0=Produzir || 1=Consumir 
};
typedef struct flag * Flag;

//Quantidade de bytes para definição das shared memories 
#define SM_QUEUE_SZ sizeof(struct queue_t) 
#define SM_PIDS_SZ  PIDS_SZ*sizeof(int)

Queue F1; //Ponteiro para F1 (shared memory)
Queue F2; //Ponteiro para F2 (shared memory)
int* pids; //Vetor com PIDs de todos os processos [pai,p1,p2,p3,p4,p5,p6,p7] (shared memory)
long int thread1p4Id; //TID da thread original do P4
Flag flagF1;
Flag flagF2;
int pipe01[2];
int pipe02[2];


//Protótipos das funções
void  consumerF1(); 
// void* consumerF2();
int   createChildren();
void  createPipes();
void  createSemaphore (sem_t * semaphore);
void  createSharedMemory (int type, int sharedMemorySize, int keySM);
void  initQueue (Queue queue);
int   isEmpty (Queue queue);
int   isFull (Queue queue);
int   next (int position);
void* p4SignalReceiver();
int   pop (Queue queue, int * value);
void  producerF1();
// void  producerF2(int pipe);
int   push (Queue queue, int value);
void* setFlagF1ToConsume();

int main () {
	printf("pai --> %d\n", getpid());

	//Criação e inicialização das Shared Memories
	srand(time(NULL));
	createSharedMemory(1, SM_QUEUE_SZ, random()); //F1
	createSharedMemory(2, SM_QUEUE_SZ, random()); //F2
	createSharedMemory(3, SM_PIDS_SZ,  random()); //pids
	createSharedMemory(4, sizeof(int), random()); //flagF1

	//Inicialização das pipes
	createPipes();

	//Cria processos filhos
	int id = createChildren();

	//P1, P2, P3
	if ( id <= 3 ){

		//Garante que o p4 chegue em p4SignalReceiver, antes dos produtores enviarem o sinal para consumo
		sleep(1); 
		
		producerF1();
	}

	//P4
	else if ( id == 4 ){
		thread1p4Id = gettid(); //Defino TID da thread principal do p4
		
		pthread_t thread2;
		pthread_create(&thread2, NULL, p4SignalReceiver, NULL);
		p4SignalReceiver();
		pthread_join(thread2, NULL);
	
	} 
	// else if ( id == 5 ){
	// 	producerF2(1);
	// } else if ( id == 6 ){
	// 	producerF2(2);
	// } else if ( id == 7 ){
	// 	pthread_t tids[2];
	// 	for (int i = 0; i < 2; i++) 
	//         pthread_create(&tids[i], NULL, consumerF2, NULL);
	//     consumerF2();
	//     for (int i = 0; i < 2; ++i) {
	// 		pthread_join(tids[i], NULL);
	//     }
	// }	

	return 0;
}

//Argumento type: 
//	1 = Ponteiro para F1
//  2 = Ponteiro para F2
//  3 = Ponteiro para vetor de PIDs
//  4 = Ponteiro para flag de controle de fila
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
  		F1 = (Queue) sharedMemory;
  		initQueue(F1); 
  	} else if (type == 2) {
  		F2 = (Queue) sharedMemory;
  		initQueue(F2);
  	} else if (type == 3) {
  		pids = (int *) sharedMemory;
  		*(pids) = getpid(); // pids[0] = Pid do processo pai
  	} else if (type == 4) {
  		flagF1 = (Flag) sharedMemory;
  		flagF1->flag = 0;
  		createSemaphore(&flagF1->mutex);
  	}
}

//Inicializa todos os valores da struct queue_t
void initQueue (Queue queue) {
	queue->fst   = 0;
	queue->lst   = 0;
	queue->count = 0;
	createSemaphore(&queue->mutex);
}

//Inicializa semáforo
void createSemaphore (sem_t * semaphore) {
	if ( sem_init(semaphore,1,1) != 0 ) {
		printf("Semaphore creation failed\n");
		exit(-1);
	}
}

//Inicializa ambas pipes do projeto
void createPipes() {
	if ( pipe(pipe01) == -1 ){ printf("Erro pipe()"); exit(-1); }
	if ( pipe(pipe02) == -1 ){ printf("Erro pipe()"); exit(-1); }
}

//Processo pai cria todos os 7 filhos
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
			printf("p%d  --> %d\n", id, getpid());
			return id;
		}
		*(pids+id) = p; //Pai recebe PID do filho criado e insere valor no vetor pids
	}

	for (int i = 0; i < 7; i++)
		wait(NULL); //Pai espera o fim da execução de todos os filhos
	
	return 0;
}

//p1, p2 e p3 produzem elementos aleatórios para F1
void producerF1() {
	int response, random;
	srand(getpid() + F1->lst); //Seed para função random() sempre muda dessa forma
	while(1) {

		sem_wait((sem_t*)&flagF1->mutex);
		if (flagF1->flag == 1){
			sem_post((sem_t*)&flagF1->mutex);	
			break; //Se a fila já estiver sendo consumida, não posso produzir
		} 
		sem_post((sem_t*)&flagF1->mutex);

		random = rand()%1000; //Gera número aleatório entre 1 e 1000
		response = push(F1, random); //Tenta inserir na F1

		if(response == 1) { //Último elemento inserido na fila

			while(kill(*(pids+4), SIGUSR1) == -1); //Tento enviar sinal para p4 consumir até ter sucesso
			break;

		} else if (response == -1) //Fila cheia
			break;
	}
}

//Tenta inserir elemento "value" na fila "queue"
int push (Queue queue, int value) {
	sem_wait((sem_t*)&queue->mutex);
	
	if (isFull(queue)) {
		sem_post((sem_t*)&queue->mutex);	
		return -1;
	}

	queue->array[queue->lst] = value; printf("%d insere %d na F1 na posicao: %d\n", getpid(), value, queue->lst);
	queue->lst = next(queue->lst);
	queue->count++;

	//Caso inserção encheu a fila, flagSendSignal == 1
	int flagSendSignal = isFull(queue); 
	
	sem_post((sem_t*)&queue->mutex);
	return flagSendSignal;
}

//Verifica se fila está cheia
int isFull (Queue queue) {
	return queue->count == QUEUE_SZ;
}

//Calcula próxima posição livre para realizar a inserção 
int next (int position) {
	return (position + 1) % QUEUE_SZ; //Inserção circular
}

//Threads de p4 aguardam envio do sinal dos produtores
void* p4SignalReceiver() {
	if (gettid() == thread1p4Id) {
		signal(SIGUSR1, (__sighandler_t) setFlagF1ToConsume);
		pause();
	}

	//Enquanto a fila não estiver pronta para consumo, não fazer nada
	while(1) {
		sem_wait((sem_t*)&flagF1->mutex);
		if (flagF1->flag == 1) { //F1 pronta pra consumo
			sem_post((sem_t*)&flagF1->mutex);
			break;
		}
		sem_post((sem_t*)&flagF1->mutex);
	}
	
	consumerF1();
}

//Bloqueia produtores de continuarem produzindo ao retirar elementos da F1
void* setFlagF1ToConsume() {
	sem_wait((sem_t*)&flagF1->mutex);
	flagF1->flag = 1; //Não produza mais! F1 pode ser consumida
	sem_post((sem_t*)&flagF1->mutex);
}

//p4 (dualThread) consome F1 e insere elementos nas pipes
void consumerF1() {
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

//Tenta retirar um elemento da fila "queue" e inserir em "value" passado por referência
int pop (Queue queue, int * value) {
	sem_wait((sem_t*)&queue->mutex);

	if (isEmpty(queue)) {
		sem_post((sem_t*)&queue->mutex);
		return -1;
	}

	*value = queue->array[queue->fst]; printf("%d remove %d da F1\n", getpid(), *value);
	queue->fst = next(queue->fst);
	queue->count--;

	sem_post((sem_t*)&queue->mutex);
	return 0;
}

//Verifica se fila está vazia
int isEmpty (Queue queue) {
	return queue->count == 0;
}

void producerF2(int pipe) {
	int value, resp, response;
	while(1) {
		
		if (pipe == 1) {
			resp = read(pipe01[0], &value, sizeof(int));
		} else if (pipe == 2) {
			resp = read(pipe02[0], &value, sizeof(int));
		}

		if(resp == -1) {
			printf("Erro na leitura do pipe0%d\n", pipe);
		} else if (resp > 0) {
			response = push(F2, value);
			if (response == -1) 
				break; 
		}
	}
}

void* consumerF2() {
	int value, response;
	while(1) {
		response = pop(F2, &value);
		if (response == 0) {
			printf("%d retirado da F2\n", value);
		} else if (response == -1) 
			break;
	}
}
