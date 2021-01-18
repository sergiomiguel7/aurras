#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>


#define BUF_MAX 1024
#define PEDIDOS_MAX 20
#define FILTERS_MAX 10
#define SERVER_FIFO "../tmp/fifo_server"
#define SERVER_STATUS "../tmp/status"

struct Filter{
	char id[12]; 
	char exec[32];
	int total;
	int available;
};	

typedef struct Pedido{
	int id;
	int pid;
	int started;
	char input[128];
	char output[32];
	char filters[128];
} pedido;

static const pedido emptyPedido; //para configurar um novo pedido


int total = 0; //usado para id do pedido
pid_t pedidos[PEDIDOS_MAX]; //pids de todos os pedidos a correr
struct Filter filters[FILTERS_MAX]; //filtros existentes na configuração
char folder[128]; //pasta dos programas de filtros
int serverPid; //pid do servidor
pedido pAux; //pedido que o processo filho está a tratar
char *executados[PEDIDOS_MAX]; //serve para recuperar os filtros no fim de um pedido

/*
 * @param path -> path do ficheiro de configuracao
 * @param filters -> array de filtros criado em memoria
 *
 * lê o ficheiro de configuração e cria os filtros necessários para
 * a execução dos pedidos
 * */
void readConfig(char* path,struct Filter filters[FILTERS_MAX]){

	int n=0, counter=0, num_filters=0;
	char buffer[BUF_MAX], temp_buffer[32];
	struct Filter *temp = malloc(sizeof(struct Filter));

	int configFd = open(path, O_RDONLY);
	if(configFd < 0){
		printf("-> %s\n", path);
		perror("Erro ao abrir ficheiro de config");
		_exit(-1);
	}

	n = read(configFd, buffer, BUF_MAX);
	for(char *p= strtok(buffer, "\n"); p!=NULL; p = strtok(NULL, "\n")){
		switch(counter){
			case 0: 
				strcpy(temp->id, p);
				break;
			case 1:
				strcpy(temp->exec, p);
				break;
			case 2: 
				temp->available = atoi(p);
				temp->total = atoi(p);
				filters[num_filters]=*temp;//add to array
				num_filters++;
				break;
		}

		if(counter == 2){
			temp = malloc(sizeof(struct Filter));
			counter = 0; //to find new filter
		}else
			counter++;

	}
	printf("Filtros definidos no ficheiro de configuração: %d\n", num_filters);
}


/*
 * neste função é enviado para o pipe status os processamentos
 * a decorrer de momento no servidor, bem como a carga de trabalho dos
 * filtros existentes
 */
void sendStatus(){

	int pid = getpid(), fd;
	char *response = malloc(sizeof(char)*2048);

	//abrir pipe status
	fd = open(SERVER_STATUS, O_WRONLY);
	if(fd < 0){
		perror("Erro ao abrir pipe status");
		return;
	}
	if(pid == serverPid){
		//servidor(processo pai) envia os filtros e a sua carga
		for(int i = 0; i < FILTERS_MAX; i++){
			if(strlen(filters[i].id) > 0){
				char* aux = malloc(sizeof(char)*128);
				sprintf(aux, "filter %s: %d/%d (disponiveis/total)\n",
						filters[i].id, filters[i].available, filters[i].total);
				strcat(response, aux);
			}
		}

	} else { //filho envia o pedido que está a tratar
		sprintf(response, "task #%d: transform %s %s %s\n", 
				pAux.id, pAux.input, pAux.output, pAux.filters);
	}


	write(fd, response, strlen(response));
	close(fd);
}


/*
 *
 *@param numFilters - numero de filtros pedidos pelo cliente
 *@param filtersExec - nomes correspondentes ao path do filtro pedido
 *@param novo - pedido feito pelo cliente
 *
 *nesta função irá ser dividido o processo de filtros no ficheiro pretendido atraves de redirecionamento e pipes anonimos, sendo que 
 *o ultimo filho criado(neto do servidor) irá redirecionar o seu output para o pipe com nome do cliente
 */
void dividirProcesso(int numFilters, char *filtersExec[FILTERS_MAX], pedido novo){

	int p[2];

	int fd = open(novo.input, O_RDONLY);
	char *aux = malloc(sizeof(novo.pid)+8);
	sprintf(aux,"../tmp/%d",novo.pid);
	int final = open(aux,O_WRONLY);

	if(final <0 || fd < 0)
	{
		perror("Opening file error");
		_exit(1);
	}

	for(int i = 0; i < numFilters ; i++){
		if(pipe(p)==-1){
			perror("pipe");
			return;
		}

		char *exec = malloc(sizeof(char)*128);
		strcat(exec,folder);
		strcat(exec,filtersExec[i]);	
		switch(fork()){
			case -1:
				perror("fork");
				return;
			case 0:
				if(i == 0){
					dup2(fd,0); //stdin do primeiro filtro é o ficheiro do cliente
					close(fd); 
				}

				close(p[0]); //fechar pipe leitura

				if(i == numFilters-1){ //ultimo filtro
					dup2(final, 1); //stdout é o pipe com nome do cliente
					close(final);
					execl(exec, exec,NULL);
					perror(exec);
					_exit(2);
				}
				else{
					dup2(p[1],1); //stdout é o pipe escrita
					close(p[1]);
					execl(exec, exec, NULL);
					perror(exec);
					_exit(3);
				}	
			default:{
					dup2(p[0],0);
					close(p[1]);
					close(p[0]);
				}}
	}

	for(int i = 0; i < numFilters; i++ )
		wait(NULL);


	_exit(0);
}


/*
 *@param novo - pedido enviado pelo cliente
 *
 *nesta função são lidos e relacionados com os filtros existentes no servidor, os filtros pedidos pelo cliente
 *se todos estiverem disponiveis o pedido é enviado para um processo filho que o irá gerir
 */
void inserirPedido(pedido novo){
	int numFilters = 0;
	char *filtersExec[FILTERS_MAX];
	char *filtros = malloc(sizeof(novo.filters));
	strcpy(filtros,novo.filters);

	for(char *c= strtok(filtros," "); c!=NULL; c = strtok(NULL, " ")){
		for(int i = 0; i < FILTERS_MAX; i++){
			if(!strcmp(filters[i].id,c)){
				if(filters[i].available == 0){
					return;
				}
			}
		}
	}

	char *filtros2 = malloc(sizeof(novo.filters));
	strcpy(filtros2,novo.filters);

	for(char *c= strtok(filtros2," "); c!=NULL; c = strtok(NULL, " ")){
		for(int i = 0; i < FILTERS_MAX; i++){
			if(!strcmp(filters[i].id,c)){
				filters[i].available--;
				filtersExec[numFilters] = filters[i].exec;
				numFilters++;
				break;
			}		
		}
	}


	//filtros necessarios disponiveis
	pid_t pid = fork();
	if(pid==0){ 
		dividirProcesso(numFilters, filtersExec, novo);
	} else { 
		char *aux = malloc(sizeof(char)*64);
		sprintf(aux,"%d %s",pid,novo.filters);

		for(int i = 0; i < PEDIDOS_MAX; i++){	
			if(executados[i] == NULL){
				executados[i]=aux;
				break;
			}
		}

		for(int i=0; i<PEDIDOS_MAX; i++){
			if(pedidos[i]==0){
				pedidos[i]=pid;
				break;
			}
		}	
	}

}

/*
 *@param buffer - buffer que entrou no pipe com nome do servidor
 *@param n - numero bytes lido
 *
 *função que verifica se as condiçoes do pedido estão conforme o pretendido,
 *se sim insere o pedido no servidor
 */
void gerirPedido(char *buffer, int n){
	int aux = 0;
	pAux = emptyPedido;
	buffer = strtok(buffer,"\n");
	for(char *c= strtok(buffer, " "); c!=NULL; c = strtok(NULL, " ")){
		switch(aux){
			case 0: 
				pAux.pid = atoi(c);
				break;		
			case 1: 
				strcpy(pAux.input, c);
				break;
			case 2: 
				strcpy(pAux.output,c);
				break;
			default:
				strcat(pAux.filters,c);
				strcat(pAux.filters," ");
		}
		aux++;
	}
	if(aux < 3){
		return;
	}
	pAux.started = 0;
	pAux.id = total;
	total++;
	inserirPedido(pAux);
}

/**
 * @param son - pid do processo filho que morreu apos concluir pedido
 *
 * nesta função será reposto o numero de filtros disponiveis
 */
void recuperaFiltro(pid_t son){
	for(int i = 0; i < PEDIDOS_MAX ; i++){
		if(pedidos[i]==son){ //pid do filho encontrado
			pedidos[i] = 0;
			for(int j = 0; j < PEDIDOS_MAX ; j++){
				if(executados[j]!=NULL){
					char *buffer = malloc(sizeof(char)*64);
					strcpy(buffer,executados[j]);
					int aux = 0;
					for(char *c= strtok(buffer, " "); c!=NULL; c = strtok(NULL, " ")){
						if(aux == 0){ 
							if(atoi(c) != son){ //processo executado corresponde ao filho que morreu
								break;
							}
						}else{
							for(int k = 0; k < FILTERS_MAX; k++){
								if(!strcmp(filters[k].id,c)){
									filters[k].available++;
									break;
								}
							}
						}
						aux++;
					}
					if(aux > 0){ //processo que foi exectuado e com filtros recuperados
						executados[j]=NULL;
						break;
					}
				}
			}
		}
	}
}

void handler(int signal){

	switch(signal){

		case SIGINT: {
				     //acabar todos os pedidos
				     if(getpid()==serverPid){
					     while(1){
						     int waiting=0;
						     for(int i =0; i<PEDIDOS_MAX; i++){
							     if(pedidos[i]!=0){
								     waiting++;
							     }		
						     }
						     if(waiting == 0){ //todos os processos acabaram
							     break;
						     }
					     }
					     unlink(SERVER_FIFO);
					     unlink(SERVER_STATUS);
					     _exit(0);
				     }
			     }
		case SIGCHLD:{
				     int status;
				     pid_t son = wait(&status);
				     if(WIFEXITED(status) && getpid()==serverPid){ 
					     recuperaFiltro(son);
				     }
				     break;
			     }
		case SIGUSR1: {
				      sendStatus(); 
			      }	     
	}	     

}

/* ./aurras config-filename filters-folder */
int main(int argc, char* argv[]){

	signal(SIGINT, handler);
	signal(SIGUSR1, handler);
	signal(SIGCHLD, handler);

	if(argc != 3){
		printf("Ficheiro de config e path de filtros necessario");
		return -1;
	}

	//criação dos fifos
	mkfifo(SERVER_FIFO, 0755);
	mkfifo(SERVER_STATUS, 0755);

	serverPid = getpid();
	int fd,n;
	char buffer[BUF_MAX];

	//indicação pasta dos filtros
	strcpy(folder, argv[2]);
	//ler ficheiro configuração
	readConfig(argv[1],filters);
	//abrir fifo servidor
	fd = open(SERVER_FIFO, O_RDONLY);


	while((n = read(fd, buffer, 256)) >= 0){
		if(n>1){
			if(n<20){
				for(int i=0; i < PEDIDOS_MAX;i++){
					if(pedidos[i]!=0){	
						kill(pedidos[i], SIGUSR1);
					}	
				}
				kill(serverPid, SIGUSR1);

			}else{
				write(1,"Pedido Recebido\n",18);
				gerirPedido(buffer, n);			
			}	
		}		
	}


	return 0;

}
