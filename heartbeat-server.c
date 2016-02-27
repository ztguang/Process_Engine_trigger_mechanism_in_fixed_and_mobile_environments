// gcc -I/usr/include/mysql/ -L/usr/lib64/mysql/ -lmysqlclient heartbeat-server.c -o heartbeat-server
// gcc -I/usr/include/mysql/ -L/usr/lib64/mysql/ -lmysqlclient -lpthread server.c -o server
// gcc heartbeat-server.c -o heartbeat-server
// indent -npro -kr -i8 -ts8 -sob -l280 -ss -ncs -cp1 *
/* heartbeat-server.c
 *
 * Copyright (c) 2015 zhang tong guang. ztguang.blog.chinaunix.net
 * Use may be in whole or in part in accordance to
 * the General Public License (GPL).
*/
/*****************************************************************************/
/*** heartbeat-server.c                                                    ***/
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <resolv.h>
#include <signal.h>
#include <sys/wait.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <mysql.h>

#define BUF_SIZE 1024			//默认缓冲区
#define NAME_SIZE 40			//客户端用户名
#define PROCESSID_SIZE 52		//

#define SERVER_PORT 11111		//监听端口
#define SERVER_HOST "127.0.0.1"	//服务器IP地址
#define LISTEN_SIZE 10			//监听队列长度

//两个有用的宏定义：检查和赋值并且检测
#define CHK(eval) if(eval < 0){perror("eval"); printf("(%d)-<%s>\n", __LINE__, __FUNCTION__); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0){perror("eval"); printf("(%d)-<%s>\n", __LINE__, __FUNCTION__); exit(-1);}

//mysql------------------
MYSQL *conn_ptr;
MYSQL_RES *res_ptr;
MYSQL_ROW sqlrow;
MYSQL_FIELD *fd;
int res, i, j;
char sql[1024];
//mysql------------------

char username[NAME_SIZE];
char processid[PROCESSID_SIZE];

int downtime, period, client;

struct sigaction act;
/*---------------------------------------------------------------------
	sig_handler - catch and send heartbeat.
 ---------------------------------------------------------------------*/
void sig_handler(int signum)
{
	char c;
	struct tcp_info info;
	int leng = sizeof(info);

	if (signum == SIGURG) {

		//读写之前，先判断socket connection是否正常
		getsockopt(client, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
		if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 

			recv(client, &c, sizeof(c), MSG_OOB);

			if (c == '?') {							//Are you alive?
				send(client, "Y", 1, MSG_OOB);		//YES!
				printf("(%d)-<%s> recv [heartbeat] from SMD\n", __LINE__, __FUNCTION__);

				//-----------------------------------------------
				//receive heartbeat from SMD, a time per 5s
				//period=currenttime - smdtime;
				memset(sql, 0x00, 1024);
				sprintf(sql, "select UNIX_TIMESTAMP(now())-UNIX_TIMESTAMP(smdtime) from user where username='%s'", username);
				//printf("sql:%s\n", sql);
				res = mysql_query(conn_ptr, sql);	//查询语句  
				if (res) {
					printf("(%d)-<%s> SELECT error:%s\n", __LINE__, __FUNCTION__, mysql_error(conn_ptr));
				} else {
					res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
					if (res_ptr) {
						sqlrow = mysql_fetch_row(res_ptr);
						period = atoi(sqlrow[0]);
					}
					mysql_free_result(res_ptr);
				}
				//-----------------------------------------------

			}
		} else {		// socket disconnected 
			printf("(%d)-<%s> SIGURG: socket disconnected FCD\n", __LINE__, __FUNCTION__);
			return;
		}

	} else if (signum == SIGCHLD)
		wait(0);
}

/*---------------------------------------------------------------------
	process requests from SMD
 ---------------------------------------------------------------------*/
void handle_message()
{
	//sleep(5);
	char buf[BUF_SIZE];
	int len, initconn = 1;
	char normterm, state;

	//heartbeat-server------------------servlet - process requests

	struct tcp_info info;
	int leng = sizeof(info);

	int bytes;
	char buffer[BUF_SIZE];
	bzero(&act, sizeof(act));
	act.sa_handler = sig_handler;
	act.sa_flags = SA_RESTART;
	sigaction(SIGURG, &act, 0);	/* connect SIGURG signal */
	if (fcntl(client, F_SETOWN, getpid()) != 0)
		perror("Can't claim SIGIO and SIGURG");
/*
	do {
		bzero(buffer, BUF_SIZE);
		bytes = recv(client, buffer, sizeof(buffer), 0);
		printf("recieve [%s] from client-%d\n", buffer, client);
		if (bytes > 0)
			send(client, buffer, bytes, 0);
	} while (bytes > 0);
//*/
	//heartbeat-server------------------servlet - process requests


	while (1) {			//communication between FCD & SMD
		period = -1;	//used to determine whether SMD access FCD continuously 

		//------------------read * from SMD
		//读写之前，先判断socket connection是否正常
		getsockopt(client, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
		if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
			bzero(buf, BUF_SIZE);
			//CHK2(len, recv(client, buf, BUF_SIZE, MSG_NOSIGNAL));	//read * from SMD
			CHK2(len, recv(client, buf, sizeof(buf), 0));	//read * from SMD
		} else {		// socket disconnected, 客户端关闭或出错，关闭socket
			printf("(%d)-<%s> socket disconnected, client: [%d]\n", __LINE__, __FUNCTION__, client);
			CHK(close(client));
			return;
		}

		//+++++++++++++++++++++++++++++++++++++++++++++++
		//------------------read username from SMD
		if (!strncmp(buf, "Jone", 4)) {
			bzero(username, NAME_SIZE);
			strncpy(username, buf, 4);
			printf("(%d)-<%s> name: %s\n", __LINE__, __FUNCTION__, username);

			//mysql------------------read normterm
			memset(sql, 0x00, 1024);
			sprintf(sql, "select normterm from user where username='%s'", username);
			res = mysql_query(conn_ptr, sql);	//查询语句  
			//printf("218: %s\n", sql);
			if (res) {
				printf("(%d)-<%s> SELECT error:%s\n", __LINE__, __FUNCTION__, mysql_error(conn_ptr));
			} else {
				res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
				if (res_ptr) {
					//printf("224:\n");
					sqlrow = mysql_fetch_row(res_ptr);
					normterm = atoi(sqlrow[0]);
				}
				mysql_free_result(res_ptr);
			}
			//mysql------------------read normterm

			continue;
		} //end if

		//------------------read processid from SMD
		if (!strncmp(buf, "2015-1119-2302-57441-1181348586", 31)) {
			bzero(processid, PROCESSID_SIZE);
			strncpy(processid, buf, 31);
			printf("(%d)-<%s> processid:%s\n", __LINE__, __FUNCTION__, processid);
			continue;
		}
		//+++++++++++++++++++++++++++++++++++++++++++++++

		//printf("283:\n");
/*
		if (!strncmp(buf, "heartbeat", 9))	//receive heartbeat from SMD, a time per 5s
		{
			//period=currenttime - smdtime;
			memset(sql, 0x00, 1024);
			sprintf(sql, "select UNIX_TIMESTAMP(now())-UNIX_TIMESTAMP(smdtime) from user where username='%s'", username);
			res = mysql_query(conn_ptr, sql);	//查询语句  
			if (res) {
				printf("server: SELECT error:%s\n", mysql_error(conn_ptr));
			} else {
				res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
				if (res_ptr) {
					sqlrow = mysql_fetch_row(res_ptr);
					period = atoi(sqlrow[0]);
				}
				mysql_free_result(res_ptr);
			}
		}
		//printf("303:\n");
//*/

		//update user_table set smdtime=currenttime where username=guestname;
		memset(sql, 0x00, 1024);
		sprintf(sql, "update user set smdtime=now() where username='%s'", username);
		res = mysql_query(conn_ptr, sql);	//update语句  
		if (res) {
			printf("(%d)-<%s> update error:%s\n", __LINE__, __FUNCTION__, mysql_error(conn_ptr));
		}
		//printf("313:\n");

		if ((0 <= period && period < 8.0) || initconn) {	//normal visits
			printf("(%d)-<%s> 0 <= period < 8.0, access FCD\n", __LINE__, __FUNCTION__);

			//判断socket connection是否正常
			getsockopt(client, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
				//send information for current process to SMD;
				CHK(send(client, processid, strlen(processid), MSG_NOSIGNAL));		//需要改写
			} else {
				CHK(close(client));
				printf("(%d)-<%s> [client: %d] not normal termination\n", __LINE__, __FUNCTION__, client);
				return;
			}

			initconn = 0;		//very important
		} //end if

		if (!strncmp(buf, "finish", 6))	//receive finish from SMD， normal termination
		{
			//update user_table set normterm=1 where username=guestname;
			memset(sql, 0x00, 1024);
			sprintf(sql, "update user set normterm=1 where username='%s'", username);
			res = mysql_query(conn_ptr, sql);	//update语句  
			//mysql_close(conn_ptr);

			//判断socket connection是否正常
			getsockopt(client, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
				printf("(%d)-<%s> [client: %d] normal terminated\n", __LINE__, __FUNCTION__, client);
			} else {
				printf("(%d)-<%s> [client: %d] not normal terminated\n", __LINE__, __FUNCTION__, client);
			}
			CHK(close(client));		//if no data from SMD, then kill handle_message
			return;					//guest finished his tasks
		} //end if

		//server restart, process is not normal end
		//after disconnect, SMD reconnect FCD
		if (downtime > 30 && !normterm || !strncmp(buf, "reconnect", 9)) {
			printf("(%d)-<%s> downtime: %d, normterm: [%d]\n", __LINE__, __FUNCTION__, downtime, normterm);
			//FCD receive current process information from SMD;
			//------------------read * from SMD

			//判断socket connection是否正常
			getsockopt(client, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {					// socket connected 
				bzero(buf, BUF_SIZE);
				CHK2(len, recv(client, buf, BUF_SIZE, MSG_NOSIGNAL));	//read * from SMD

				printf("(%d)-<%s> FCD recv process information from SMD:%s\n", __LINE__, __FUNCTION__, buf);
				//update correlative tables where processid=processid;
				memset(sql, 0x00, 1024);
				sprintf(sql, "update process set state=1 where processid='%s'", processid);
				res = mysql_query(conn_ptr, sql);	//update语句 
				//mysql_close(conn_ptr);
			} else {
				CHK(close(client));
				printf("(%d)-<%s> [client: %d] not normal termination\n", __LINE__, __FUNCTION__, client);
				return;
			} //end if
		} //end if

		sleep(1);
	} //end while

	printf("(%d)-<%s> end while\n", __LINE__, __FUNCTION__);
	CHK(close(client));
	//exit(0);
	return;
} //end handle_message


/*---------------------------------------------------------------------
	main - set up client and begin the heartbeat.
 ---------------------------------------------------------------------*/
int main(int count, char *strings[])
{
	//mysql_close(conn_ptr);
	//mysql------------------
	conn_ptr = mysql_init(NULL);
	if (!conn_ptr) {
		printf("(%d)-<%s> mysql_init failed\n", __LINE__, __FUNCTION__);
		return EXIT_FAILURE;
	}
	conn_ptr = mysql_real_connect(conn_ptr, "localhost", "root", "123456", "test", 0, NULL, 0);
	//root 为用户名 123456为密码 test为要连接的database  

	if (conn_ptr) {
		printf("(%d)-<%s> MYSQL Connection success\n", __LINE__, __FUNCTION__);
	} else {
		printf("(%d)-<%s> MYSQL Connection failed\n", __LINE__, __FUNCTION__);
		return EXIT_FAILURE;
	}

	//selftime: the most current time of FCD being active;
	//downtime: suppose server restart time is 30s;
	//select selftime from fcd_state_table where id=1;
	//downtime=currenttime - selftime;
	//update fcd_state_table set selftime=currenttime where id=1;
	//-----------
	memset(sql, 0x00, 1024);
	sprintf(sql, "select UNIX_TIMESTAMP(now())-UNIX_TIMESTAMP(selftime) from fcd_state where id=1");
	res = mysql_query(conn_ptr, sql);	//查询语句  
	if (res) {
		printf("(%d)-<%s> SELECT error:%s\n", __LINE__, __FUNCTION__, mysql_error(conn_ptr));
	} else {
		res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
		if (res_ptr) {
			sqlrow = mysql_fetch_row(res_ptr);
			downtime = atoi(sqlrow[0]);
		}
		mysql_free_result(res_ptr);
	}

	//update fcd_state_table set selftime=currenttime where id=1;
	memset(sql, 0x00, 1024);
	sprintf(sql, "update fcd_state set selftime=now() where id=1");
	res = mysql_query(conn_ptr, sql);	//update语句  
	if (res) {
		printf("(%d)-<%s> update error:%s\n", __LINE__, __FUNCTION__, mysql_error(conn_ptr));
	}
	//-----------

	//mysql_close(conn_ptr);
	//mysql------------------

	bzero(&act, sizeof(act));
	act.sa_handler = sig_handler;
	act.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	if (sigaction(SIGCHLD, &act, 0) != 0)
		perror("sigaction()");

	int listener;		//监听socket
	struct sockaddr_in addr, peer;
	addr.sin_family = PF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
	socklen_t socklen;
	socklen = sizeof(struct sockaddr_in);

	CHK2(listener, socket(PF_INET, SOCK_STREAM, 0));	//初始化监听socket

	// 设置套接字选项避免地址使用错误  
	int on = 1;
	if ((setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
		perror("server: setsockopt failed");
		exit(EXIT_FAILURE);
	}

	CHK(bind(listener, (struct sockaddr *)&addr, sizeof(addr)));	//绑定监听socket

	//printf("listen\n");
	CHK(listen(listener, LISTEN_SIZE));	//设置监听
	printf("(%d)-<%s> listening\n", __LINE__, __FUNCTION__);

	while (1) {
		//client = accept(listener, (struct sockaddr *)&peer, &socklen);
		CHK2(client, accept(listener, (struct sockaddr *)&peer, &socklen));
		printf("(%d)-<%s> accept client---[%d]\n", __LINE__, __FUNCTION__, client);
		if (client > 0) {
			if (fork() == 0) {
				CHK(close(listener));
				handle_message();
				return 0;
			} else
				CHK(close(client));
		} else
			perror("accept()");
	} //end while

	return 0;
} //end main

