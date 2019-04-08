#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#ifdef WIN32
#include <WinSock2.h>
#include <Windows.h>
#pragma comment(lib, "WSOCK32.LIB")
#endif

#include "tcp_listener.h"
#include "tcp_session.h"

static int add_client_for_listen(struct session* s, void* param) 
{
	fd_set* set = (fd_set*)param;
	FD_SET(s->to_client_socket, set);
	return 0;
}
 
static int process_online_session(struct session* s, void* param)
{
	fd_set* set = (fd_set*)param;
	
	//��������
	if (FD_ISSET(s->to_client_socket, set))
	{
		session_data_receive(s);
	}

	return 0;
}


void start_tcp_listener(unsigned short port) 
{
#ifdef WIN32
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
#endif // WIN32

	int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_socket == INVALID_SOCKET)
	{
		goto FAILED;
	}
	
	printf("Bind port %d...\n", port);

	struct sockaddr_in server_listen_addr;
	server_listen_addr.sin_addr.S_un.S_addr = inet_addr("0.0.0.0");
	server_listen_addr.sin_port = htons(port);
	server_listen_addr.sin_family = AF_INET;
	int ret = bind(server_socket, (const struct sockaddr*)&server_listen_addr, sizeof(server_listen_addr));

	if (ret != 0)
	{
		printf("Bind failed on %s, %d\n", "127.0.0.1", port);
		goto FAILED;
	}
	
	ret = listen(server_socket, 128);  //����ȴ���������� ����128���ܽ�������

	if (ret != 0)
	{
		printf("Listen failed\n");
		goto FAILED;
	}

	init_session_manager(0);
	fd_set server_fd_set;

	while (1)
	{
		FD_ZERO(&server_fd_set);
		FD_SET(server_socket, &server_fd_set);  //���еȴ��������
		
		foreach_online_session(add_client_for_listen, (void*)&server_fd_set);

		printf("Waitting...\n");
		int ret = select(0, &server_fd_set, NULL, NULL, NULL);
		if (ret < 0)
		{
			printf("server_fd_set handler invalid.\n");
			assert(1 == 0);    //��ֹ����
		}
		else if (ret == 0)  //��ʱ
		{
			continue;
		}

		//�����ݽ��� 
		printf("Get data...\n");

		if (FD_ISSET(server_socket, &server_fd_set)) //�пɶ����ݾ������
		{
			struct sockaddr_in to_client_addr;
			int len = sizeof(to_client_addr);
			int to_client_socket = accept(server_socket, (struct sockaddr_in*)&to_client_addr, &len);

			if (to_client_socket != INVALID_SOCKET)
			{
				printf("Client %s:%d comming...\n", inet_ntoa(to_client_addr.sin_addr), ntohs(to_client_addr.sin_port));
				add_session(to_client_socket, inet_ntoa(to_client_addr.sin_addr), ntohs(to_client_addr.sin_port));
			}
		}

		foreach_online_session(process_online_session, (void*)&server_fd_set);
		clear_closed_session();
	}

	exit_session_manager();

FAILED:
	if (server_socket != INVALID_SOCKET)
	{
		closesocket(server_socket);
	}

#ifdef WIN32
	WSACleanup();
#endif
}