#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>


#define SERVER_FIFO "../tmp/fifo_server"
#define FILTERS_MAX 10


typedef struct Pedido {
	char input[128];
	char output[32];
	char filters[128];
}pedido, *LPedido;


/*
 *@param str - path do pipe com nome aberto pelo cliente
 *@param p - pedido efetuado pelo cliente ao servidor
 * 
 * função que tem como objetivo esperar pela resposta do servidor e guardar o ficheiro	
 * */
void recebeFicheiro(char str[100],LPedido p){
	pid_t pid = fork();
	if(pid != 0)
		return;

	//abrir pipe para leitura
	int fd = open(str, O_RDONLY|O_NONBLOCK);

	if(fd<0){
		perror("Error opening client fifo");
		_exit(1);
	}

	int n,aux = 0;
	char buffer[4096];
	//abrir ficheiro que irá ser criado com a resposta do servidor
	int final = open(p->output,O_CREAT | O_WRONLY, 0666);
	if(final<0)
		perror("Ficheiro final");
	while(1){
		if((n = read(fd,buffer,sizeof(buffer)))>0){
			write(final,buffer,n);
			aux = 1;
		}else if(aux == 1){
			break;
		}
	}
	//eliminar pipe com nome
	unlink(str);
	_exit(0);
}

/*
 *@param sign - sinal obtido por interrupção
 * */
void handler(int sign){

	if(sign==SIGALRM){
		_exit(0);
	}

}

int main(int argc, char *argv[]){

	signal(SIGALRM, handler);

	if(argc != 2 &&  argc < 5){
		perror("Argumentos insuficentes");
		_exit(1);
	}
	//descritores de ficheiros, sendo que fd2 é o pipe do servidor
	int fd,fd2 = open(SERVER_FIFO, O_WRONLY|O_NONBLOCK);

	if(fd2<0){
		perror("Error opening server fifo");
		_exit(1);
	}
	int n=0;
	char *buffer;
	//pedido de transformação ao servidor
	if(argc != 2){

		//criar pipe por onde se vai receber o ficheiro final
		char str[100];
		sprintf(str,"../tmp/%d",getpid());
		mkfifo(str, 0755);
			
		LPedido p = (LPedido)malloc(sizeof(pedido));

		strcpy(p->input,argv[2]);
		strcpy(p->output,argv[3]);

		for(int i=4;i<argc;i++){	
			strcat(p->filters, argv[i]);			
			if(i + 1 < argc){
				strcat(p->filters," ");
			}
		}
		buffer = malloc(sizeof(pedido));
		
		//enviar pedido
		sprintf(buffer,"%d %s %s %s\n", getpid(),p->input, p->output, p->filters);
		write(fd2, buffer, strlen(buffer));
		//receber resposta
		recebeFicheiro(str,p);
	}
	else{ 
		//pedir estado do servidor
		char *aux = malloc(sizeof(char)*24);
		buffer = malloc(sizeof(char)*2048);
		aux="status";
		write(fd2, aux, strlen(aux));
		fd = open("../tmp/status", O_RDONLY);
		alarm(1);
		if(fd<0)
			perror("Erro ao abrir pipe status");
		else
			while(1){ //imprimir estado
				if((n = read(fd, buffer,2048)) > 0){	
					write(1, buffer, n);
				}
			}
	}
	close(fd);
	return 0;

}

