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
#define AMOUNT_DATA 10 //Quantidade de elementos que devem ser processados por p7
#define INTERVAL 1000

//Estrutura das filas 1 e 2
struct queue_t { 
	sem_t mutex;
    long int bwCon;
	int bwF2;
	int fst, lst, count, bwProd; 
	int array[QUEUE_SZ];
};
typedef struct queue_t * Queue;

//Estrutura da flag utilizada para controle de produção e consumo da F1
struct flagF1_t {
	sem_t mutex;
	int flag; // 0=Produzir || 1=Consumir 
};
typedef struct flagF1_t * Flag1;

//Estrutura da flag utilizada para controle de produção e consumo da F2 e métricas do código
struct flagF2_t {
	sem_t mutex;
	int flag; // 1 = Encerrar p5 e p6, pois p7 já processou quantidade necessária de elementos
	int counterP5, counterP6; //Contadores com a quantidade de elementos processados por p5 e p6. item (b)
	int counterTotal; //Contador de elementos para threads do P7
	int counterEach[INTERVAL+1]; //Vetor com a quantidade de cada número aleatório processado por p7. item (c)
};
typedef struct flagF2_t * Flag2;

//Quantidade de bytes para definição das shared memories 
#define SM_QUEUE_SZ sizeof(struct queue_t) 
#define SM_PIDS_SZ  PIDS_SZ*sizeof(int)
#define SM_FLAG1_SZ sizeof(struct flagF1_t)
#define SM_FLAG2_SZ sizeof(struct flagF2_t)

Queue F1; //Ponteiro para F1 (shared memory)
Queue F2; //Ponteiro para F2 (shared memory)
int* pids; //Vetor com PIDs de todos os processos [pai,p1,p2,p3,p4,p5,p6,p7] (shared memory)
long int thread1p4Id; //TID da thread original do P4
long int thread2p4Id; //TID da thread secundária do P4

long int thread1p7Id; //TID da thread 1 do P7
long int thread2p7Id; //TID da thread 2 do P7
long int thread3p7Id; //TID da thread 3 do P7

Flag1 flagF1; //Ponteiro para flag de controle de consumo e produção da F1
Flag2 flagF2; //Ponteiro para flag de controle de consumo e produção da F2
int pipe01[2];
int pipe02[2];

//Protótipos das funções
int      alternationProd (int id, int mod);
long int alternationCon ();
long int alternationF2 (long int identifier);
void  	 consumerF1(); 
void* 	 consumerF2();
int   	 createChildren();
void  	 createPipes();
void  	 createSemaphore (sem_t * semaphore);
void  	 createSharedMemory (int type, int sharedMemorySize, int keySM);
void  	 initQueue (Queue queue);
int   	 isEmpty (Queue queue);
int   	 isFull (Queue queue);
int   	 next (int position);
void* 	 p4SignalReceiver();
int   	 popF1 (int * value);
int 	 popF2 (int * value);
void  	 producerF1();
void  	 producerF2(int process);
int   	 pushF1 (int value, int id, int mod);
int      pushF2 (int value, int id, int mod);
void* 	 setFlagF1ToConsume();
void   	 setFlagF1ToProduce();
void  	 printResult();

int main () {
	clock_t begin = clock();

	//Criação e inicialização das Shared Memories
	srand(time(NULL));
	createSharedMemory(1, SM_QUEUE_SZ, random()); //F1
	createSharedMemory(2, SM_QUEUE_SZ, random()); //F2
	createSharedMemory(3, SM_PIDS_SZ,  random()); //pids
	createSharedMemory(4, SM_FLAG1_SZ, random()); //flagF1
	createSharedMemory(5, SM_FLAG2_SZ, random()); //flagF2

	//Inicialização das pipes
	createPipes();

	//Cria processos filhos
	int id = createChildren();


	//Processo pai
	if ( id == 0 ) {
		clock_t end = clock();
		printResult((double)(end - begin) / CLOCKS_PER_SEC);
	}	

	//P1, P2, P3
	else if ( id <= 3 ){
		while(1) {
			F1->bwProd = 0;
			producerF1(id);
		}
	}

	// P4
	else if ( id == 4 ){
		thread1p4Id = gettid(); //Defino TID da thread principal do p4
		F1->bwCon = thread1p4Id;
		
		pthread_t thread2;
		pthread_create(&thread2, NULL, p4SignalReceiver, NULL);
		p4SignalReceiver();
		pthread_join(thread2, NULL);
	} 

	//P5, P6
	else if ( id == 5 || id == 6){
		producerF2(id);
	}

	//P7
	else if ( id == 7 ){
		pthread_t tids[2];
		for (int i = 0; i < 2; i++) 
	        pthread_create(&tids[i], NULL, consumerF2, NULL);
	    consumerF2();

	    for (int i = 0; i < 2; ++i) {
			pthread_join(tids[i], NULL);
	    }
	}

	return 0;
}

//Argumento type: 
//	1 = Ponteiro para F1
//  2 = Ponteiro para F2
//  3 = Ponteiro para vetor de PIDs
//  4 = Ponteiro para flag de controle da F1
//  5 = Ponteiro para flag de controle da F2
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
  		flagF1 = (Flag1) sharedMemory;
  		createSemaphore(&flagF1->mutex);
  	} else if (type == 5) {
  		flagF2 = (Flag2) sharedMemory;
  		F2->bwProd = 0;
  		createSemaphore(&flagF2->mutex);
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

	sem_wait((sem_t*)&F1->mutex); //Pai fecha semáforo
	for(id=1; id<=7; id++){
		p = fork();
		if ( p < 0 ) {
			printf("fork failed\n");
			exit(-1);
		}
		if ( p == 0 ) {
			if ( id != 4 ) {
				//Processos != p4 esperam a criação dos demais filhos
				//p4 deve chegar no pause() antes que p1, p2 e p3 preencham a fila, 
				//por isso não espera a criação dos demais
				sem_wait((sem_t*)&F1->mutex); 
			}
			printf("p%d  --> %d\n", id, getpid());
			return id;
		}
		*(pids+id) = p; //Pai recebe PID do filho criado e insere valor no vetor pids
	}

	for (int i = 0; i < 7; ++i) {
		sem_post((sem_t*)&F1->mutex); //Pai libera todos os filhos
	}

	for (int i = 0; i < 7; i++){
		wait(NULL); //Pai espera o fim da execução de todos os filhos
	}

	return 0;
}

//p1, p2 e p3 produzem elementos aleatórios para F1
void producerF1(int id) {
	int response, random;
	srand(getpid() + F1->lst); //Seed para função random() sempre muda dessa forma
	while(1) {

		sem_wait((sem_t*)&flagF1->mutex);
		if (flagF1->flag == 1){
			sem_post((sem_t*)&flagF1->mutex);	
			break; //Se a fila já estiver sendo consumida, não posso produzir
		} 
		sem_post((sem_t*)&flagF1->mutex);

		random = rand()%INTERVAL; //Gera número aleatório entre 1 e 1000
		response = pushF1(random, id-1, 3); //Tenta inserir na F1

		if(response == 1) { //Último elemento inserido na fila

			while(kill(*(pids+4), SIGUSR1) == -1); //Tento enviar sinal para p4 consumir até ter sucesso
			break;

		} else if (response == -1) //Fila cheia
			break;
	}
}

//Tenta inserir elemento "value" na fila "queue"
int pushF1 (int value, int id, int mod) {
    int flagSendSignal;
    while(F1->bwProd != id);

    if (isFull(queue)) {
        F1->bwProd = -2;
        return -1;
    }

    F1->array[F1->lst] = value; 
    //printf("%d insere %d na F1 na posicao: %d\n", getpid(), value, queue->lst);	
    
    F1->lst = next(F1->lst);
    F1->count++;

    //Caso inserção encheu a fila, flagSendSignal == 1
    flagSendSignal = isFull(F1); 

    F1->bwProd = alternationProd(id, mod);
	return flagSendSignal;
}

//Calcula próximo processo a executar no busy wait
int alternationProd (int id, int mod) {
	return (id + 1) % mod; //Inserção circular
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

	if (gettid() != thread1p4Id) 
		thread2p4Id = gettid();

	while(1) {
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
		setFlagF1ToProduce();
	}
}

//Bloqueia produtores de continuarem produzindo ao retirar elementos da F1
void* setFlagF1ToConsume() {
	sem_wait((sem_t*)&flagF1->mutex);
	flagF1->flag = 1; //Não produza mais! F1 pode ser consumida
	sem_post((sem_t*)&flagF1->mutex);
}

//p4 (dualThread) consome F1 e escreve elementos nas pipes
void consumerF1() {
	int response, value;
	while(1) {
		response = popF1(&value);
		if (response == 0) {
			
			if (thread1p4Id == gettid()){
				// printf("%ld mandou %d pra pipe01\n", gettid(), value);
				write(pipe01[1], &value, sizeof(int));
			}
			else {
				// printf("%ld mandou %d pra pipe02\n", gettid(), value);
				write(pipe02[1], &value, sizeof(int));
			}

		} else if (response == -1) 
			break;
	}
}

//Libera produtores para produzir elementos para F1
void setFlagF1ToProduce() {
	sem_wait((sem_t*)&flagF1->mutex);
	flagF1->flag = 0; //Produza mais! F1 pode não pode ser consumida
	sem_post((sem_t*)&flagF1->mutex);
}

//Tenta retirar um elemento da fila "queue" e inserir em "value" passado por referência
int popF1 (int * value) {
    while(F1->bwCon != gettid());

    if (isEmpty(F1)) {
        F1->bwCon = thread1p4Id;
        return -1;
    }
    *value = F1->array[F1->fst]; 

    //printf("%d remove %d da F1\n", getpid(), *value);	
    
    F1->fst = next(F1->fst);
    F1->count--;

    F1->bwProd = alternationCon();
	return 0;
}

//Calcula próxima thread a executar no busy wait
long int alternationCon () {
	if (gettid() == thread1p4Id) {
		return thread2p4Id;
	} else {
		return thread1p4Id;
	}
}


//Verifica se fila está vazia
int isEmpty (Queue queue) {
	return queue->count == 0;
}

//Lê elementos das pipes e insere em F2
void producerF2(int process) {
	
	int value, resp, response;

	while(1) {
		//Se já produziu todos elementos da pipe, encerrar P5 e P6
		sem_wait((sem_t*)&flagF2->mutex);
		if (flagF2->flag)  { 
			sem_post((sem_t*)&flagF2->mutex);
			break;
		}
		sem_post((sem_t*)&flagF2->mutex);

		if (process == 5) 
			resp = read(pipe01[0], &value, sizeof(int)); //Tentativa de leitura de pipe01
		else if (process == 6) 
			resp = read(pipe02[0], &value, sizeof(int)); //Tentativa de leitura de pipe02	
		
		if(resp == -1) {
			
			printf("Erro na leitura do pipe0%d\n", process-4);
			break;

		} else if (resp > 0) { 

			//Se já produziu todos elementos da pipe, encerrar p5 e p6
			//Double check pois pode ter dado read antes de p7 alterar a flag
			sem_wait((sem_t*)&flagF2->mutex);
			if (flagF2->flag)  { 
				sem_post((sem_t*)&flagF2->mutex);
				break;
			}
			sem_post((sem_t*)&flagF2->mutex);
			
			response = pushF2 (value, process); //Tento colocar na F2
			
			if (response == -1)
				break;
			else {
				sem_wait((sem_t*)&flagF2->mutex);
				//Incrementa contador de elementos processados por p5 ou p6
				if(process == 5) flagF2->counterP5++;
				else if (process == 6) flagF2->counterP6++;
				sem_post((sem_t*)&flagF2->mutex);
			}
		}
	}
}

//Tenta inserir elemento "value" na fila 2
int pushF2 (int value, int id) {

    int flagSendSignal;
    while(F2->bwF2 != id);

    if (isFull(F2)) {
        F2->bwF2 = alternationF2(id);
        return -1;
    }

    F2->array[F2->lst] = value; 
    //printf("%d insere %d na F1 na posicao: %d\n", getpid(), value, queue->lst);	
    
    F2->lst = next(F2->lst);
    F2->count++;

    //Caso inserção encheu a fila, flagSendSignal == 1
    flagSendSignal = isFull(F2); 

    F2->bwF2 = alternationF2(id);
	return flagSendSignal;
}


long int alternationF2 (long int identifier) {

	switch (identifier)
	{
		case thread1p7Id: //thread 1 de P7
			return thread2p7Id;
		break;

		case thread2p7Id: //thread 2 de P7
			return thread3p7Id;
		break;

		case thread3p7Id: //thread 3 de P7
			return 5;
		break;

		case 5: //processo 5
			return 6;
		break;

		case 6: //processo 6
			return thread1p7Id;
		break;
	}
}

void* consumerF2() {
	int value, response;
	while(1) {
		response = popF2(&value);
		//qual thread?
		switch (i)
		{
				case 0: 
					thread1p7Id = tids[i];
				break;
				case 1: 
					thread2p7Id = tids[i];
				break;
				case 2: 
					thread3p7Id = tids[i];
				break;
			}

		if (response == 0) {
			sem_wait((sem_t*)&flagF2->mutex);
			
			flagF2->counterTotal++;
			printf("%d retirado da F2\n", value);

			flagF2->counterEach[value]++; //Incrementa 1 na posição correspondente ao elemento aleatório processado por p7
			
			if (flagF2->counterTotal >= AMOUNT_DATA)  { //Se processei quantidade total de elementos que desejo
				for (int i = 1; i <= 7; ++i) {
					kill(*(pids+i), SIGTERM) == -1; //Mato todos os filhos e me suicido
				}
			}
			
			sem_post((sem_t*)&flagF2->mutex);
		}
	}
}

//Tenta retirar um elemento da fila 2 e inserir em "value" passado por referência
int popF2 (int * value) {

    while(F2->bwF2 != id);

	if (isEmpty(queue)) {
		F2->bwF2 = alternationF2(gettid());
		return -1;
	}

	*value = queue->array[queue->fst]; 

	// if (queue == F1) {
	// 	printf("%d remove %d da F1\n", getpid(), *value);	
	// } else {
	// 	printf("%d remove %d da F2\n", getpid(), *value);	
	// }
	
	queue->fst = next(queue->fst);
	queue->count--;

	F2->bwF2 = alternationF2(gettid());
	return 0;
}


void printResult(double timeSpent) {
	printf("\na)\n\t*Tempo de execucao do programa: %lf\n", timeSpent);

	printf("\nb)\n\t*Quantidade de valores processados por p5: %d\n", flagF2->counterP5);
	printf("\t*Quantidade de valores processados por p6: %d\n", flagF2->counterP6);
	
	int mode = 0;
	int higher = flagF2->counterEach[0];
	for (int i = 0; i <= INTERVAL+1; ++i) {
		if (higher < flagF2->counterEach[i]) {
			higher = flagF2->counterEach[i];
			mode = i;
		}
	}
	printf("\nc)\n\t*Moda: %d\n", mode);

	int min;
	for (int i = 0; i <= INTERVAL+1; ++i) {
		if (flagF2->counterEach[i] > 0) {
			min = i;
			break;
		}
	}
	printf("\t*Valor minimo: %d\n", min);

	int max;
	for (int i = INTERVAL+1; i >= 0; --i) {
		if (flagF2->counterEach[i] > 0) {
			max = i;
			break;
		}
	}
	printf("\t*Valor maximo: %d\n", max);
}
