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
#define NAME_SIZE 20	//추가된 최대 이름 사이즈

void * handle_clnt(void * arg);
void send_msg(char * msg, int len);
void error_handling(char * msg);
//추가된 함수이름들
void * handle_clnt_extend(void * arg);
void send_msg_unicast(char * msg, int len, int sock);
void send_msg_broadcast(char * msg, int len);

int clnt_cnt=0;
//int clnt_socks[MAX_CLNT];	기존 소켓 파일 배열 대신 아래의 구조체 기반 배열 사용
typedef struct {
	int sock;
	char name[NAME_SIZE];
} clnt_info;
clnt_info clnt_infos[MAX_CLNT];	//추가한 배열

pthread_mutex_t mutx;

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	int clnt_adr_sz = sizeof(clnt_adr); //init size
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
	
	//추가된 로직. <<사용자 구분용 이름 저장>>
	while(1)
	{
    clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);

    // 1) 클라이언트가 보낸 이름을 받을 버퍼
    char name_buf[NAME_SIZE];
    int  len;

    // 2) 중복검사 반복
    while (1) {
        len = read(clnt_sock, name_buf, NAME_SIZE-1);
        if (len <= 0) {
            close(clnt_sock);	//clnt_infos에 저장하지 않았으니 바로 통신 종료 가능
            goto CONTINUE_ACCEPT;	//오류시 다시 상단 accept으로 돌아감
        }
        name_buf[len] = '\0';

        // 잠금 후 중복 검사
        pthread_mutex_lock(&mutx);
        int dup = 0;
        for (int i = 0; i < clnt_cnt; i++) {
            if (strcmp(clnt_infos[i].name, name_buf) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup) {
            // 중복 없으면 배열에 저장
			clnt_infos[clnt_cnt].sock = clnt_sock;
            strncpy(clnt_infos[clnt_cnt].name, name_buf, NAME_SIZE);
            clnt_cnt++;
            pthread_mutex_unlock(&mutx);

            write(clnt_sock, "OK", 2);   // 클라이언트에 승인 통보
            break;                       // 루프 탈출 → 스레드 생성
        }
        pthread_mutex_unlock(&mutx);

        // 중복이면 클라이언트에 통보 후 재입력 대기
        write(clnt_sock, "DUP", 3);
    }

    pthread_create(&t_id, NULL, handle_clnt_extend, (void*)&clnt_sock);
    pthread_detach(t_id);
    printf("Connected: %s (%s)\n", name_buf, inet_ntoa(clnt_adr.sin_addr));
		
	CONTINUE_ACCEPT:
    	;

	}
	close(serv_sock);
	return 0;
}

// ---**여기서 부터 추가로 만든 함수**---
void *handle_clnt_extend(void *arg) {
    int clnt_sock = *((int *)arg);
    char name_msg[NAME_SIZE + BUF_SIZE];
    char sender_name[NAME_SIZE];
	char dest_name[NAME_SIZE];
    char msg[BUF_SIZE];
    int str_len;

    while ((str_len = read(clnt_sock, name_msg, sizeof(name_msg) - 1)) > 0) {
        name_msg[str_len] = '\0';

        // 전체 payload: "[sender] message"
        char *space_ptr = strchr(name_msg, ' ');
        if (space_ptr == NULL) continue;
		*space_ptr = '\0';

        strcpy(sender_name, name_msg);

        // 메시지 본문 분리
        strcpy(msg, space_ptr + 1);
        
        *space_ptr = ' ';

        //printf("%s send a message", sender_name);
        write(1, "test1", 5);
        // 브로드캐스트 또는 유니캐스트 처리
        if (msg[0] != '@') {
            //브로드캐스트
            //printf("broadcast");
            write(1, "test2", 5);
            send_msg_broadcast(name_msg, str_len);
        } else {
            //printf("unicast");
            write(1, "test3", 5);
            //유니캐스트 //@target content 형태로 분리
            char *delimeter = strchr(msg + 1, ' ');
            if (delimeter == NULL) continue;
            *delimeter = '\0';

			strcpy(dest_name, msg+1);
			snprintf(name_msg, NAME_SIZE+BUF_SIZE, "%s %s", sender_name, delimeter+1);
			str_len = strlen(name_msg);

            if (strcasecmp(dest_name, "all") == 0) {
                send_msg_broadcast(name_msg, str_len);
            } else {
                int dest_sock = -1;
                pthread_mutex_lock(&mutx);
                for (int i = 0; i < clnt_cnt; i++) {
                    if (strcmp(clnt_infos[i].name, dest_name) == 0) {
                        write(1, clnt_infos[i].name, strlen(clnt_infos[i].name));
                        dest_sock = clnt_infos[i].sock;
                        break;
                    }
                }
                pthread_mutex_unlock(&mutx);
                if (dest_sock != -1) {
                    send_msg_unicast(name_msg, str_len, dest_sock);
                }
            }
        }
    }

    // 클라이언트 접속 종료 시 정리
    pthread_mutex_lock(&mutx);
    for (int i = 0; i < clnt_cnt; i++) {
        if (clnt_infos[i].sock == clnt_sock) {
            for (int j = i; j < clnt_cnt - 1; j++) {
                clnt_infos[j] = clnt_infos[j + 1];
            }
            clnt_cnt--;
            break;
        }
    }
    pthread_mutex_unlock(&mutx);
    close(clnt_sock);
    return NULL;
}
void send_msg_unicast(char * msg, int len, int sock)   // send to some one
{
	pthread_mutex_lock(&mutx);
	write(sock, msg, len);
	pthread_mutex_unlock(&mutx);
}
void send_msg_broadcast(char * msg, int len)   // send to some one
{
	int i;
	pthread_mutex_lock(&mutx);
	for(i=0; i<clnt_cnt; i++)
		write(clnt_infos[i].sock, msg, len);
	pthread_mutex_unlock(&mutx);
}
// ---**여기까지 추가된 함수**---
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
		if(clnt_sock==clnt_infos[i].sock)
		{
			while(i <clnt_cnt-1)
			{
				clnt_infos[i]=clnt_infos[i+1];	//clnt_socks를 사용하지 않음으로 이부분만 수정
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
		write(clnt_infos[i].sock, msg, len);//clnt_socks를 사용하지 않음으로 이부분만 수정
	pthread_mutex_unlock(&mutx);
}
void error_handling(char * msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}
