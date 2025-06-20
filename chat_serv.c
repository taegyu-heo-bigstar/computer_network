#include <stdio.h> 
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUF_SIZE 100
#define MAX_CLNT 256
#define NAME_SIZE 20	// **추가** 클라이언트가 가질 수 있는 이름의 최대 길이

void * handle_clnt(void * arg);
void send_msg(char * msg, int len);
void error_handling(char * msg);

void * handle_clnt_extend(void * arg);
void * handle_connectionInfo(int sock);
void send_msg_unicast(char * msg, int len, int sock);

typedef struct client_information{
	int sock;	// **추가** 클라이언트과 연결된 소켓의 파일 디스크립터(클라이언트 구분용)
	char name[NAME_SIZE];	// **추가 클라이언트의 이름**
} clnt_info;

int clnt_cnt=0;
int clnt_socks[MAX_CLNT];
pthread_mutex_t mutx;
clnt_info clnts[MAX_CLNT];	// **추가** 클라이언트의 정보를 저장하는 배열

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	int clnt_adr_sz;
	char clnt_name[NAME_SIZE];
	int* p_clnt_sock;
	pthread_t t_id;
	if(argc!=2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
  
	pthread_mutex_init(&mutx, NULL);
	serv_sock=socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family=AF_INET; 
	serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
	serv_adr.sin_port=htons(atoi(argv[1]));
	
	if(bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr))==-1)
		error_handling("bind() error");
	if(listen(serv_sock, 5)==-1)
		error_handling("listen() error");
	
	while(1)
	{
		printf("server can access in loop");
		clnt_adr_sz=sizeof(clnt_adr);
		clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
		p_clnt_sock = malloc(sizeof(int));
		*p_clnt_sock = clnt_sock;
		printf("server success accept");
		pthread_mutex_lock(&mutx);
		handle_connectionInfo(*p_clnt_sock);
		pthread_mutex_unlock(&mutx);
	
		pthread_create(&t_id, NULL, handle_clnt_extend, (void*)p_clnt_sock);
		pthread_detach(t_id);
		printf("Connected client IP: %s \n", inet_ntoa(clnt_adr.sin_addr));
		printf("name: %s \n", clnt_name);
	}

	close(serv_sock);
	return 0;
}
// 추가 함수
void * handle_connectionInfo(int sock){
		int str_len = 0;
		char name[NAME_SIZE];

		clnt_socks[clnt_cnt]=sock;
		int flag = 1;
		while (flag){
			printf("server can access in set info loop by flag");
			if((str_len = read(sock, name, sizeof(name))) <= 0){
				send_msg_unicast("unknown error", 5, sock);
    			return NULL;
			}
			name[str_len]=0;

			for (int i = 0; i < clnt_cnt; i++){
				 if (strcmp(clnts[i].name, name) == 0) {
					send_msg_unicast("DUP", 3, sock);
					flag++; break;
				}
			}
			flag--;
		}
	
		strncpy(clnts[clnt_cnt].name, name, NAME_SIZE);
		clnts[clnt_cnt].name[str_len] = '\0';
		clnt_cnt++;
		
		send_msg_unicast("OK", 2, sock);
}
//추가로 만든 함수
void * handle_clnt_extend(void * arg){
	int clnt_sock=*((int*)arg);
	int str_len=0, i;
	char msg[BUF_SIZE];
	char name[NAME_SIZE];
	char * space_ptr = strchr(msg, ' ');
	
	free(arg);
	printf("test1 meg");
	while ((str_len=read(clnt_sock, msg, sizeof(msg)))!=0){
		if (msg[0] != '@') send_msg(msg, str_len);	//만약 @안쓰면 브로드 캐스트
		else{									//@를 썼다면, name과 msg 분리
			space_ptr = strchr(msg, ' ');
			if (space_ptr == NULL){
				strcpy(name, msg + 1);
			}
			else{
				size_t name_len = space_ptr - msg;
				str_len -= name_len;

				strncpy(name, msg, name_len);
				name[name_len] = '\0';

				strncpy(msg, msg+name_len, str_len);
			}
		}

		if (name == "all" || name == "ALL"){	//만약 name이 all이면 브로드캐스트
			send_msg(msg, str_len);
		}
		else{
			int dest;
			for (int i = 0; i < clnt_cnt; i++){
				if (clnts[i].name == name) {dest = clnts[i].sock; break;}
			}
			send_msg_unicast(msg, str_len, dest);	//만약 name이 특정 이름이면 clnt에게 unicast
		}
	}
	pthread_mutex_lock(&mutx);
	for(i=0; i<clnt_cnt; i++)   // remove disconnected client
	{
		if(clnt_sock==clnt_socks[i])
		{
			while(i <clnt_cnt-1)
			{
				clnt_socks[i]=clnt_socks[i+1];
				  i++;

			}

			break;
		}
	}
	clnt_cnt--;
	pthread_mutex_unlock(&mutx);
	close(clnt_sock);
	return NULL;
}
void * handle_clnt(void * arg)
{
	int clnt_sock=*((int*)arg);
	int str_len=0, i;
	char msg[BUF_SIZE];
	
	while((str_len=read(clnt_sock, msg, sizeof(msg)))!=0)
		send_msg(msg, str_len);
	
	pthread_mutex_lock(&mutx);
	for(i=0; i<clnt_cnt; i++)   // remove disconnected client
	{
		if(clnt_sock==clnt_socks[i])
		{
			while(i <clnt_cnt-1)
			{
				clnt_socks[i]=clnt_socks[i+1];
				  i++;

			}

			break;
		}
	}
	clnt_cnt--;
	pthread_mutex_unlock(&mutx);
	close(clnt_sock);
	return NULL;
}
void send_msg(char * msg, int len)   // send to all
{
	int i;
	pthread_mutex_lock(&mutx);
	for(i=0; i<clnt_cnt; i++)
		write(clnt_socks[i], msg, len);
	pthread_mutex_unlock(&mutx);
}
void send_msg_unicast(char * msg, int len, int sock)   // send to some one
{
	pthread_mutex_lock(&mutx);
	write(sock, msg, len);
	pthread_mutex_unlock(&mutx);
}
void error_handling(char * msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}