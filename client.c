
/*
# This code is released under GNU GPL v2,v3
# Author: Tongguang Zhang
# Date: 2015-12-22
*/

// gcc -I/usr/include/mysql/ -L/usr/lib64/mysql/ -lmysqlclient -lpthread client.c -o client
// gcc client.c -o client
// gcc -lpthread client.c -o client
// indent -npro -kr -i8 -ts8 -sob -l280 -ss -ncs -cp1 *

//Description: guest access FCD by using SMD, there are two parts: browser on SMD and daemon written in c language

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <mysql.h>

//#endif
#define BUF_SIZE 1024		//默认缓冲区
#define NAME_SIZE 40		//客户端用户名
#define PROCESSID_SIZE 52	//
#define BROWSERMSG_SIZE 62
#define SERVER_PORT 11111	//监听端口
#define SERVER_HOST "127.0.0.1"	//服务器IP地址
#define BROWSER_PORT 22222	//监听端口
#define BROWSER_HOST "127.0.0.1"	//BROWSER本地IP地址
#define EPOLL_RUN_TIMEOUT -1	//epoll的超时时间
#define EPOLL_SIZE 1		//epoll监听的客户端的最大数目, that is pipe_reader
#define LISTEN_SIZE 3		//监听队列长度

#define STR_WELCOME "Welcome to seChat! You ID is : Client #%d"
#define STR_MESSAGE "Client #%d>> %s"
#define STR_NOONE_CONNECTED "Noone connected to server expect you!"
#define CMD_EXIT "EXIT"

//两个有用的宏定义：检查和赋值并且检测
#define CHK(eval) if(eval < 0){perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0){perror("eval"); exit(-1);}

int startfcd = 0;		//SMD 和 FCD 通信的启动开关
char message[BUF_SIZE];
double period;			//used to determine whether SMD access FCD continuously
time_t start, now;
int sock;

//daemon WAIT on SMD that waiting data sent by browser
//browser send " guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish " to daemon WAIT
//arg refer to pipefd
void *handle_browser(void *arg)
{
	//socket to listen
	int listener;		//监听socket
	struct sockaddr_in addr, peer;
	addr.sin_family = PF_INET;
	addr.sin_port = htons(BROWSER_PORT);
	addr.sin_addr.s_addr = inet_addr(BROWSER_HOST);
	socklen_t socklen;
	socklen = sizeof(struct sockaddr_in);
	int len;
	int client, starting = 1;	//starting: grest start to run tasks on browser

	CHK2(listener, socket(PF_INET, SOCK_STREAM, 0));	//初始化监听socket

	// 设置套接字选项避免地址使用错误
	int on = 1;
	if ((setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
		perror("client: setsockopt failed: browser");
		exit(EXIT_FAILURE);
	}

	CHK(bind(listener, (struct sockaddr *)&addr, sizeof(addr)));	//绑定监听socket

	//printf("listen\n");
	CHK(listen(listener, LISTEN_SIZE));	//设置监听
	printf("client: listening browser\n");
	//socket to listen

	int pipe_write = *((int *)arg);
	char browsermsg[BROWSERMSG_SIZE];

	while (1) {
		CHK2(client, accept(listener, (struct sockaddr *)&peer, &socklen));
		printf("client: accepted browser\n");

		while (1) {
			//------------------read * from browser
			bzero(browsermsg, BROWSERMSG_SIZE);
			//接受guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish
			CHK2(len, recv(client, browsermsg, BROWSERMSG_SIZE, MSG_NOSIGNAL));
			if (len == 0)	//客户端关闭或出错，关闭socket
			{
				printf("client: browser close\n");
				//CHK(close(client));
				break;
			}

			if (starting) {
				time(&start);
				starting = 0;
			}

			printf("client: receive: %s from browser\n", browsermsg);
			CHK(write(pipe_write, browsermsg, strlen(browsermsg)));	//send guestname, finish
			printf("client: %d\n", client);
			//CHK(close(client));
		}
	}
	return NULL;
}				//end handle_browser

//communication between SMD & FCD, SMD 和 FCD 通信
//arg is sock, arg is NULL
void *handle_fcd(void *arg)
{
	struct sockaddr_in seraddr;
	seraddr.sin_family = PF_INET;
	seraddr.sin_port = htons(SERVER_PORT);
	seraddr.sin_addr.s_addr = inet_addr(SERVER_HOST);

	//mysql------------------
	MYSQL *conn_ptr;
	MYSQL_RES *res_ptr;
	MYSQL_ROW sqlrow;
	MYSQL_FIELD *fd;
	int res, i, j;
	char sql[1024];

	conn_ptr = mysql_init(NULL);
	if (!conn_ptr) {
		printf("client-fcd: mysql_init failed\n");
		return EXIT_FAILURE;
	}
	conn_ptr = mysql_real_connect(conn_ptr, "localhost", "root", "123456", "test", 0, NULL, 0);
	//root 为用户名 123456为密码 test为要连接的database  

	if (conn_ptr) {
		printf("client-fcd: MYSQL Connection success\n");
	} else {
		printf("client-fcd: MYSQL Connection failed\n");
		return EXIT_FAILURE;
	}
	//mysql_close(conn_ptr);
	//mysql------------------

	//int sock = *((int*)arg);

	int len;
	char buf[BUF_SIZE];
	char processid[PROCESSID_SIZE] = { 0, };

	sleep(1);

	while (1)		//communication between SMD & FCD, SMD 和 FCD 通信
	{
		printf("client-fcd: startfcd: %d\n", startfcd);
		if (startfcd)	//wait main() to set startfcd=1, SMD 和 FCD 通信的启动开关
		{
			CHK(send(sock, "heartbeat", strlen("heartbeat"), MSG_NOSIGNAL));	//send heartbeat to FCD;
			printf("client-fcd: try to connect sock again\n");
			bzero(buf, BUF_SIZE);
			CHK2(len, recv(sock, buf, BUF_SIZE, MSG_NOSIGNAL));	//read * from FCD
			if (len == 0)	//server关闭或出错，关闭socket
			{
				printf("client-fcd: server close: %d\n", sock);
				CHK(close(sock));
				//return NULL;
				CHK2(sock, socket(PF_INET, SOCK_STREAM, 0));
				CHK(connect(sock, (struct sockaddr *)&seraddr, sizeof(seraddr)) < 0);
			}

			if (!strncmp(buf, "heartbeat", 9))	//receive heartbeat from FCD, a time per 5s
			{
				//FCD receive current process information from FCD;
				//------------------read * from FCD
				bzero(buf, BUF_SIZE);
				CHK2(len, recv(sock, buf, BUF_SIZE, MSG_NOSIGNAL));	//read * from FCD
				printf("SMD receive current process information from FCD:%s\n", buf);

				//update correlative tables where processid=processid;
				//todo

				memset(sql, 0x00, 1024);
				sprintf(sql, "update process set state=1 where processid=='%s'", processid);
				res = mysql_query(conn_ptr, sql);	//update语句
			}

			time(&now);
			period = difftime(now, start);
			printf("client-fcd: period: %d\n", period);

			if (0 <= period < 8.0) {	//normal visits
				time(&start);
			}

			if (period > 30.0) {	//server shutdown or connectiong to FCD is abnormal
				//trigger SMD;
				//redirect to localhost and invoke task in SMD
				printf("trigger SMD; redirect to localhost and invoke task in SMD\n");
				while (1) {
					//send heartbeat to FCD; to probe whether FCD is active
					CHK(send(sock, "heartbeat", strlen("heartbeat"), MSG_NOSIGNAL));
					if (!strncmp(buf, "heartbeat", 9))	//receive heartbeat from FCD, a time per 5s
					{
						//stop accessing MPE;
						//send information for current process to FCD;
						printf("stop accessing MPE; send information for current process to FCD\n");
						CHK(send(sock, processid, strlen(processid), MSG_NOSIGNAL));	//need to modify
						sleep(3);	//waiting for FCD to update it's database
						//trigger FCD;
						//redirect to FCD and invoke task in FCD;
						printf("trigger FCD, redirect to FCD and invoke task in FCD\n");

						sleep(60);
					}
				}
			}
		}		//end if
		sleep(5);	//send heartbeat request every 5s to FPE
	}			//end while
}				//end handle_fcd

/*
    create two threads：
        
	handle_browser：等待browser，将browser的信息通过管道写给 main thread
	handle_fcd：接受 FCD 的信息，将从 handle_browser 接受到的信息发送给 FCD
*/
int main(int argc, char *argv[])
{
	int pid, pipe_fd[2], epfd;
	int res, len;
	char buf[BUF_SIZE];
	struct sockaddr_in seraddr;
	seraddr.sin_family = PF_INET;
	seraddr.sin_port = htons(SERVER_PORT);
	seraddr.sin_addr.s_addr = inet_addr(SERVER_HOST);

	static struct epoll_event ev, events[2];
	ev.events = EPOLLIN | EPOLLET;

	CHK2(sock, socket(PF_INET, SOCK_STREAM, 0));
	CHK(connect(sock, (struct sockaddr *)&seraddr, sizeof(seraddr)) < 0);

	CHK(pipe(pipe_fd));

	CHK2(epfd, epoll_create(EPOLL_SIZE));

	//ev.data.fd = sock;
	//CHK(epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev));

	ev.data.fd = pipe_fd[0];	// reader
	CHK(epoll_ctl(epfd, EPOLL_CTL_ADD, pipe_fd[0], &ev));

	//------- create thread: handle_browser
	pthread_t writer;
	int rt = pthread_create(&writer, NULL, handle_browser, (void *)&pipe_fd[1]);
	if (-1 == rt) {
		printf("thread creation error\n");
		return -1;
	}
	//------- create thread: handle_browser

	//------- create thread: handle_fcd
	pthread_t fcd;
	//int rtfcd = pthread_create(&fcd, NULL, handle_fcd, (void *)&sock);
	int rtfcd = pthread_create(&fcd, NULL, handle_fcd, NULL);
	if (-1 == rtfcd) {
		printf("client-main: thread creation error\n");
		return -1;
	}
	//------- create thread: handle_fcd

	//------- the following is main thread, daemon MAIN on SMD

	char username[NAME_SIZE];
	char processid[PROCESSID_SIZE];

	char browsermsg[BROWSERMSG_SIZE];

	int epoll_events_count;
	char *prt_c;

	while (1) {
		//read * from browser, event_driven
		//guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish
		CHK2(epoll_events_count, epoll_wait(epfd, events, 2, EPOLL_RUN_TIMEOUT));
		for (int i = 0; i < epoll_events_count; i++) {
			if (events[i].data.fd == pipe_fd[0])	//从browser接受信息
			{
				bzero(&browsermsg, BROWSERMSG_SIZE);
				CHK2(res, read(pipe_fd[0], browsermsg, BROWSERMSG_SIZE));
				printf("client-main: browsermsg: %s\n", browsermsg);
				if (!strncmp(browsermsg, "finish", 6))	//send finish to FCD
				{
					CHK(send(sock, "finish", strlen("finish"), MSG_NOSIGNAL));
					//send information for current process to FCD;
					//sprintf(processid, "%s", "current process information"); //read from database actually
					//CHK(send(sock, processid, strlen(processid), 0)); //need to modify
					//close(pipe_fd[0]);
					//close(pipe_fd[1]);
					sleep(3);
					pthread_cancel(fcd);

					startfcd = 0;
					CHK2(sock, socket(PF_INET, SOCK_STREAM, 0));
					CHK(connect(sock, (struct sockaddr *)&seraddr, sizeof(seraddr)) < 0);

					//------- create thread: handle_fcd
					//rtfcd = pthread_create(&fcd, NULL, handle_fcd, (void *)&sock);
					rtfcd = pthread_create(&fcd, NULL, handle_fcd, NULL);
					if (-1 == rtfcd) {
						printf("client-main: thread creation error\n");
						return -1;
					}
					//------- create thread: handle_fcd

					//close(sock);
					//return 0;
				} else if (!strncmp(browsermsg, "guestname", 9)) {	//send username to FCD
					prt_c = &browsermsg[10];
					printf("client-main: username: %s\n", prt_c);
					//CHK(send(sock, prt_c, strlen(prt_c), 0));     //send username to FCD
					CHK(send(sock, prt_c, strlen(prt_c), MSG_NOSIGNAL));	//send username to FCD
					bzero(username, NAME_SIZE);
					strncpy(username, prt_c, NAME_SIZE);
				} else if (!strncmp(browsermsg, "processid", 9)) {	//send processid to FCD
					prt_c = &browsermsg[10];
					printf("client-main: processid: %s\n", prt_c);
					//CHK(send(sock, prt_c, strlen(prt_c), 0));     //send processid to FCD
					CHK(send(sock, prt_c, strlen(prt_c), MSG_NOSIGNAL));	//send processid to FCD
					bzero(processid, PROCESSID_SIZE);
					strncpy(processid, prt_c, PROCESSID_SIZE);
					startfcd = 1;	//SMD 和 FCD 通信的启动开关
					//printf("startfcd = 1\n");
				}

			}	// end if
		}		// end for
	}			//end while
}				//end main
