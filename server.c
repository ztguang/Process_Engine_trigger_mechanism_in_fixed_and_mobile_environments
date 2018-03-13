// gcc -lpthread server.c -o server
// gcc -I/usr/include/mysql/ -L/usr/lib64/mysql/ -lmysqlclient -lpthread server.c -o server
// indent -npro -kr -i8 -ts8 -sob -l280 -ss -ncs -cp1 *

/*
# This code is released under GNU GPL v2,v3
# Author: Tongguang Zhang
# Date: 2015-12-22
*/

/*
create table user(
	id int unsigned not null primary key auto_increment,
	username char(40) not NULL,
	normterm char default NULL,
	smdtime datetime default NULL);
insert into user values(1, 'Jone', 0, NULL);

create table process(
	id int unsigned not null primary key auto_increment,
	processid char(52) default NULL,
	username char(40) not NULL,
	state char default '0',
	exectime datetime default NULL);

create table fcd_state(
	id int unsigned not null primary key auto_increment,
	selftime datetime default NULL);
insert into fcd_state values(1, NULL);
//update fcd_state set selftime=now() where id=1;
*/
//Description: when FCD start, this algorithm will run as a deamon

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <mysql.h>

//#endif
#define BUF_SIZE 1024			//默认缓冲区
#define NAME_SIZE 40			//客户端用户名
#define PROCESSID_SIZE 52		//
#define SERVER_PORT 11111		//监听端口
#define SERVER_HOST "127.0.0.1"	//服务器IP地址
#define EPOLL_RUN_TIMEOUT -1	//epoll的超时时间
#define EPOLL_SIZE 10000		//epoll监听的客户端的最大数目
#define LISTEN_SIZE 10			//监听队列长度

#define STR_WELCOME "Welcome to seChat! You ID is : Client #%d"
#define STR_MESSAGE "Client #%d>> %s"
#define STR_NOONE_CONNECTED "Noone connected to server expect you!"
#define CMD_EXIT "EXIT"

//两个有用的宏定义：检查和赋值并且检测
#define CHK(eval) if(eval < 0){perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0){perror("eval"); exit(-1);}

int downtime;
void *handle_message(void *arg);

int main(int argc, char *argv[])
{
	//mysql------------------
	MYSQL *conn_ptr;
	MYSQL_RES *res_ptr;
	MYSQL_ROW sqlrow;
	MYSQL_FIELD *fd;
	int res, i, j;
	char sql[1024];

	conn_ptr = mysql_init(NULL);
	if (!conn_ptr) {
		printf("server: mysql_init failed\n");
		return EXIT_FAILURE;
	}
	conn_ptr = mysql_real_connect(conn_ptr, "localhost", "root", "123456", "test", 0, NULL, 0);
	//root 为用户名 123456为密码 test为要连接的database  

	if (conn_ptr) {
		printf("server: MYSQL Connection success\n");
	} else {
		printf("server: MYSQL Connection failed\n");
		return EXIT_FAILURE;
	}

	//selftime: the most current time of FCD being active;
	//downtime: suppose server restart time is 30s;
	//select selftime from fcd_state_table where id=1;
	//downtime=currenttime - selftime;
	//update fcd_state_table set selftime=currenttime where id=1;
	//----------------------------------
	memset(sql, 0x00, 1024);
	sprintf(sql, "select UNIX_TIMESTAMP(now())-UNIX_TIMESTAMP(selftime) from fcd_state where id=1");
	res = mysql_query(conn_ptr, sql);	//查询语句  
	if (res) {
		printf("server: SELECT error:%s\n", mysql_error(conn_ptr));
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
		printf("server: update error:%s\n", mysql_error(conn_ptr));
	}
	//----------------------------------

	mysql_close(conn_ptr);
	//mysql------------------

	int listener;		//监听socket
	struct sockaddr_in addr, peer;
	addr.sin_family = PF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
	socklen_t socklen;
	socklen = sizeof(struct sockaddr_in);

	int client;

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
	printf("server: listening\n");

	while (1) {
		//printf("accept\n");
		CHK2(client, accept(listener, (struct sockaddr *)&peer, &socklen));
		printf("server: accept client - %d\n", client);

		pthread_t reader;
		int rt = pthread_create(&reader, NULL, handle_message, (void *)&client);
		if (-1 == rt) {
			printf("server: thread creation error\n");
			return -1;
		}
	}
} //end main

void *handle_message(void *arg)
{
	//mysql------------------
	MYSQL *conn_ptr;
	MYSQL_RES *res_ptr;
	MYSQL_ROW sqlrow;
	MYSQL_FIELD *fd;
	int res, i, j;
	char sql[1024];

	conn_ptr = mysql_init(NULL);
	if (!conn_ptr) {
		printf("server: mysql_init failed\n");
		return EXIT_FAILURE;
	}
	conn_ptr = mysql_real_connect(conn_ptr, "localhost", "root", "123456", "test", 0, NULL, 0);
	//root 为用户名 123456为密码 test为要连接的database  

	if (conn_ptr) {
		printf("server: mysql Connection success\n");
	} else {
		printf("server: mysql Connection failed\n");
		return EXIT_FAILURE;
	}
	//mysql_close(conn_ptr);
	//mysql------------------

	int client = *((int *)arg);
	//sleep(5);
	char buf[BUF_SIZE];
	int len, initconn = 1;
	char normterm, state;
	char username[NAME_SIZE];
	char processid[PROCESSID_SIZE];

/*
	//mysql------------------read processid
	memset(sql, 0x00, 1024);
	//select processid from process_table where username=guestname and state=active and exectime≈current;
	sprintf(sql, "select processid from process where username='%s' and state='1' and (exectime < now() and exectime > DATE_ADD(now(),INTERVAL -10 SECOND))", username);
	res = mysql_query(conn_ptr, sql);	//查询语句  
	if (res) {
		printf("SELECT error:%s\n", mysql_error(conn_ptr));
	} else {
		res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
		if (res_ptr) {
			sqlrow = mysql_fetch_row(res_ptr);
			snprintf(processid, sizeof(processid), "%s", sqlrow[0]);
		}
		mysql_free_result(res_ptr);
	}
	//mysql_close(conn_ptr);
	//mysql------------------
*/

	while (1) {		//communication between FCD & SMD
/*
		//mysql------------------read smdtime
		memset(sql, 0x00, 1024);
		sprintf(sql, "select smdtime from user where username='%s'", username);
		res = mysql_query(conn_ptr, sql);	//查询语句  
		if (res) {
			printf("SELECT error:%s\n", mysql_error(conn_ptr));
		} else {
			res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
			if (res_ptr) {
				sqlrow = mysql_fetch_row(res_ptr);
				smdtime = sqlrow[0];
			}
			mysql_free_result(res_ptr);
		}
		//mysql------------------
*/

		int period = -1;	//used to determine whether SMD access FCD continuously 

		//------------------read * from SMD
		bzero(buf, BUF_SIZE);
		CHK2(len, recv(client, buf, BUF_SIZE, MSG_NOSIGNAL));	//read * from SMD
		if (len == 0)	//客户端关闭或出错，关闭socket
		{
			printf("server: cc close-client: %d\n", client);
			CHK(close(client));
			mysql_close(conn_ptr);
			return NULL;
		}
		//+++++++++++++++++++++++++++++++++++++++++++++++
		//------------------read username from SMD
		if (!strncmp(buf, "Jone", 4)) {
			bzero(username, NAME_SIZE);
			strncpy(username, buf, 4);
			printf("server: name: %s\n", username);

			//mysql------------------read normterm
			memset(sql, 0x00, 1024);
			sprintf(sql, "select normterm from user where username='%s'", username);
			res = mysql_query(conn_ptr, sql);	//查询语句  
			//printf("218: %s\n", sql);
			if (res) {
				printf("server: SELECT error:%s\n", mysql_error(conn_ptr));
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
		}
		//------------------read processid from SMD
		if (!strncmp(buf, "2015-1119-2302-57441-1181348586", 31)) {
			bzero(processid, PROCESSID_SIZE);
			strncpy(processid, buf, 31);
			printf("server: processid: %s\n", processid);
			continue;
		}
		//+++++++++++++++++++++++++++++++++++++++++++++++

		//printf("283:\n");

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

		//update user_table set smdtime=currenttime where username=guestname;
		memset(sql, 0x00, 1024);
		sprintf(sql, "update user set smdtime=now() where username='%s'", username);
		res = mysql_query(conn_ptr, sql);	//update语句  
		if (res) {
			printf("server: update error:%s\n", mysql_error(conn_ptr));
		}
		//printf("313:\n");

		if ((0 < period < 8) || initconn) {	//normal visits
			printf("server: 0 < period < 8\n");
			//send heartbeat to SMD;
			CHK(send(client, "heartbeat", strlen("heartbeat"), MSG_NOSIGNAL));
			//send information for current process to SMD;
			CHK(send(client, processid, strlen(processid), MSG_NOSIGNAL));	//need to modify
			initconn = 0;
		}

		if (!strncmp(buf, "finish", 6))	//receive finish from SMD， normal termination
		{
			//update user_table set normterm=1 where username=guestname;
			memset(sql, 0x00, 1024);
			sprintf(sql, "update user set normterm=1 where username='%s'", username);
			res = mysql_query(conn_ptr, sql);	//update语句  
			mysql_close(conn_ptr);
			printf("server: finish client: %d\n", client);
			close(client);
			return NULL;	//guest finished his tasks
		}
		//server restart, process is not normal end
		//after disconnect, SMD reconnect FCD
		if (downtime > 30 && !normterm || !strncmp(buf, "reconnect", 9)) {
			printf("server: downtime: %d, normterm: %d\n", downtime, normterm);
			//FCD receive current process information from SMD;
			//------------------read * from SMD
			bzero(buf, BUF_SIZE);
			CHK2(len, recv(client, buf, BUF_SIZE, MSG_NOSIGNAL));	//read * from SMD
			if (len == 0)	//客户端关闭或出错，关闭socket
			{
				printf("server: dd close-client: %d\n", client);
				CHK(close(client));
				mysql_close(conn_ptr);
				return NULL;
			}
			printf("server: FCD receive current process information from SMD:%s\n", buf);

			//update correlative tables where processid=processid;
			memset(sql, 0x00, 1024);
			sprintf(sql, "update process set state=1 where processid='%s'", processid);
			res = mysql_query(conn_ptr, sql);	//update语句 
			mysql_close(conn_ptr);
			return NULL;	//guest finished his tasks
		}

		sleep(1);
	}
	//return len;
	//return NULL;
} //end handle_message
