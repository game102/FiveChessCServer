#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcp_iocp.h"
#include "tcp_session.h"

#ifdef WIN32
#include <WinSock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "odbccp32.lib")
#endif

enum {
	IOCP_ACCEPT = 0,
	IOCP_RECV,
	IOCP_WRITE,
};

#define MAX_RECV_SIZE 8192

struct io_package {
	WSAOVERLAPPED overlapped;
	int opt_type;  //��ǰ��������
	char pkg[MAX_RECV_SIZE];
	int pkg_size;
	WSABUF wsabuffer;
};

static DWORD WINAPI
ServerWorkThread(LPVOID lParam)
{
	HANDLE iocp = (HANDLE)lParam;
	DWORD dwTrans;
	struct session* s;
	struct io_package* io_data_ptr;

	while (1)
	{
		clear_closed_session();

		//�ȴ�iocp����¼�����
		dwTrans = 0;
		s = NULL;  //���ص��û�����
		io_data_ptr = NULL;
		int ret = GetQueuedCompletionStatus(iocp, &dwTrans, (LPDWORD)&s, 
			(LPOVERLAPPED*)&io_data_ptr, WSA_INFINITE);

		if (ret == 0)
		{
			printf("iocp error\n");
			continue;
		}
	
		//iocp���ѹ����߳�
		printf("iocp have complete event\n");

		if (dwTrans == 0)
		{
			close_session(s);
			free(io_data_ptr);
			continue;
		}

		switch (io_data_ptr->opt_type)
		{
			case IOCP_RECV: {
				io_data_ptr->pkg[dwTrans] = 0;
				printf("IOCP recv %d��%s\n", dwTrans, io_data_ptr->pkg);

				// ������������ɺ������Ҫ�ټ�һ����������;
				DWORD dwRecv = 0;
				DWORD dwFlags = 0;
				int ret = WSARecv(s->to_client_socket, &(io_data_ptr->wsabuffer),
					1, &dwRecv, &dwFlags,
					&(io_data_ptr->overlapped), NULL);
			}
			break;
		}
	}
	return 0;
}


void start_server(int port)
{
#ifdef WIN32
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
#endif // WIN32

	//1���� 2�� 3����
	int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_socket == INVALID_SOCKET)
	{
		goto FAILED;
	}

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

	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (iocp == NULL)
	{
		goto FAILED;
	}

	CreateThread(NULL, 0, ServerWorkThread, (LPVOID)iocp, 0, 0);

	while (1)
	{
		struct sockaddr_in to_client_addr;
		int len = sizeof(to_client_addr);
		int to_client_socket = accept(server_socket, (struct sockaddr_in*)&to_client_addr, &len);

		if (to_client_socket != INVALID_SOCKET)
		{
			printf("Client %s:%d comming...\n", inet_ntoa(to_client_addr.sin_addr), ntohs(to_client_addr.sin_port));
		}

		struct session* s = add_session(to_client_socket, inet_ntoa(to_client_addr.sin_addr), ntohs(to_client_addr.sin_port));
		
		//to_client_socket�������iocp ���¼�����ʱ��������s
		CreateIoCompletionPort((HANDLE)to_client_socket, iocp, (DWORD)s, 0);

		struct io_package* io_data = malloc(sizeof(struct io_package));
		memset(io_data, 0, sizeof(struct io_package));
		io_data->opt_type = IOCP_RECV;
		io_data->wsabuffer.buf = io_data->pkg;
		io_data->wsabuffer.len = MAX_RECV_SIZE - 1;

		//�첽��������
		DWORD dwRecv;
		DWORD dwFlags = 0;
		WSARecv(to_client_socket, &io_data->wsabuffer, 1, &dwRecv, &dwFlags,
			&io_data->overlapped, NULL);  
		//&io_data->overlapped ���ص�ServerWorkThread��GetQueuedCompletionStatus��overlapped

	}


	//�߳�1���� �߳�2���� ��ʽ����1���߳��㹻

FAILED:
	if (iocp != NULL)
	{
		CloseHandle(iocp);
	}

	if (server_socket != INVALID_SOCKET)
	{
		closesocket(server_socket);
	}

#ifdef WIN32
	WSACleanup();
#endif
}
