#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcp_iocp.h"
#include <WinSock2.h>
#include <mswsock.h>
#include <windows.h>



#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "odbccp32.lib")

#include "tcp_session.h"
#include "../3rd/http_parser/http_parser.h"
#include "../3rd/crypt/sha1.h"
#include "../3rd/crypt/base64_encoder.h"

char *wb_accept = "HTTP/1.1 101 Switching Protocols\r\n" \
"Upgrade:websocket\r\n" \
"Connection: Upgrade\r\n" \
"Sec-WebSocket-Accept: %s\r\n" \
"WebSocket-Location: ws://%s:%d/chat\r\n" \
"WebSocket-Protocol:chat\r\n\r\n";


enum {
	IOCP_ACCEPT = 0,
	IOCP_RECV,
	IOCP_WRITE,
};

#define MAX_RECV_SIZE 8192
struct io_package {
	WSAOVERLAPPED overlapped;
	int opt_type;  //��ǰ��������
	int accpet_sock;
	WSABUF wsabuffer;
	unsigned char pkg[MAX_RECV_SIZE];
};

static void
do_accept(SOCKET l_sock, HANDLE iocp) {
	struct io_package* pkg = malloc(sizeof(struct io_package));
	memset(pkg, 0, sizeof(struct io_package));

	pkg->wsabuffer.buf = pkg->pkg;
	pkg->wsabuffer.len = MAX_RECV_SIZE - 1;
	pkg->opt_type = IOCP_ACCEPT;

	DWORD dwBytes = 0;
	SOCKET client = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	int addr_size = (sizeof(struct sockaddr_in) + 16);
	pkg->accpet_sock = client;

	AcceptEx(l_sock, client, pkg->wsabuffer.buf, 0/*pkg->wsabuffer.len - addr_size* 2*/,
		addr_size, addr_size, &dwBytes, &pkg->overlapped);
}

static void
do_recv(SOCKET client_fd, HANDLE iocp) {
	// �첽��������;
	// ʲô���첽? recv 8K���ݣ��������ʱ��û�����ݣ�
	// ��ͨ��ͬ��(����)�̹߳��𣬵ȴ����ݵĵ���;
	// �첽�������û�����ݷ�����Ҳ�᷵�ؼ���ִ��;
	struct io_package* io_data = malloc(sizeof(struct io_package));
	// ��0����ҪĿ����Ϊ������overlapped��0;
	memset(io_data, 0, sizeof(struct io_package));

	io_data->opt_type = IOCP_RECV;
	io_data->wsabuffer.buf = io_data->pkg;
	io_data->wsabuffer.len = MAX_RECV_SIZE - 1;

	// ������recv������;
	// 
	DWORD dwRecv = 0;
	DWORD dwFlags = 0;
	int ret = WSARecv(client_fd, &(io_data->wsabuffer),
		1, &dwRecv, &dwFlags,
		&(io_data->overlapped), NULL);
}


static char header_key[64];
static char client_ws_key[128];
static int
on_header_field(http_parser* h_parser, const char* at, size_t length)
{
	strncpy(header_key, at, length);
	header_key[length] = 0;
	return 0;
}


static int
on_header_value(http_parser* h_parser, const char* at, size_t length)
{
	if (strcmp(header_key, "Sec-WebSocket-Key") != 0) {
		return 0;
	}
	//Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ== (�ͻ����������)
	strncpy(client_ws_key, at, length);
	client_ws_key[length] = 0;
	printf("%s\n", client_ws_key);
	return 0;
}

static void
process_ws_http_str(int sock, char* http_str)
{
	http_parser h_parser;
	http_parser_init(&h_parser, HTTP_REQUEST);

	struct http_parser_settings s;
	http_parser_settings_init(&s);

	s.on_header_field = on_header_field;
	s.on_header_value = on_header_value;

	http_parser_execute(&h_parser, &s, http_str, strlen(http_str));

	// ��һ��http�����ݸ����ǵ�client,����websocket����
	static char key_migic[256];
	const char* migic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	sprintf(key_migic, "%s%s", client_ws_key, migic);

	int sha1_size = 0; // ��ż��ܺ�����ݳ���
	int base64_len = 0;
	char* sha1_content = crypt_sha1(key_migic, strlen(key_migic), &sha1_size);
	char* b64_str = base64_encode(sha1_content, sha1_size, &base64_len);
	// end 

	strncpy(key_migic, b64_str, base64_len);
	key_migic[base64_len] = 0;
	printf("key_migic:%s\n", key_migic);

	// �����http�ı��Ļظ����ǵ�websocket��������Ŀͻ��ˣ�
	// ����websocket���ӡ�
	static char accept_buffer[256];
	sprintf(accept_buffer, wb_accept, key_migic, "127.0.0.1", 8001);
	send(sock, accept_buffer, strlen(accept_buffer), 0);
}

static void
ws_send_data(int sock, unsigned char* data, unsigned int len) {
	static unsigned char send_buffer[8096];

	int send_len = 0;
	send_buffer[0] = 0x81;
	if (len <= 125) {
		send_buffer[1] = len;
		send_len = 2;
	}
	else if (len < 0xffff) {
		send_buffer[1] = 126;
		send_buffer[2] = (len & 0x00ff);
		send_buffer[3] = ((len & 0xff00) >> 8);
		send_len = 4;
	}
	else {
		send_buffer[1] = 127;
		send_buffer[2] = (len & 0x000000ff);
		send_buffer[3] = ((len & 0x0000ff00) >> 8);
		send_buffer[4] = ((len & 0x00ff0000) >> 16);
		send_buffer[5] = ((len & 0xff000000) >> 24);
		send_buffer[6] = 0;
		send_buffer[7] = 0;
		send_buffer[8] = 0;
		send_buffer[9] = 0;
		send_len = 10;
	}
	for (int i = 0; i < (int)len; i++) {
		send_buffer[send_len + i] = data[i];
	}
	send_len += len;
	send(sock, send_buffer, send_len, 0);
}

static void
on_ws_recv_data(struct session* s, unsigned char* pkg_data, int pkg_len) {
	unsigned char data_len = pkg_data[1];
	data_len &= 0x7f;

	unsigned char* mask = NULL;
	unsigned char* data = NULL;
	unsigned int len = 0;

	/*package size�䳤
	������2�ֽ�,��1λ��1, ʣ��7λ�õ�һ������(0, 127); 2^7 -1
	1���Ϊ125���ڵĳ���,ֱ�ӱ�ʾ��
	2���Ϊ126��ʾ����2���ֽڱ�ʾ��С, 2^16-1  
	3���Ϊ127��ʾ�����8���ֽ������ݵĳ���; 2^64-1
	*/

	if (data_len <= 125) {
		mask = pkg_data + 2;  //mark ����Ϊ����֮��� 4 ���ֽ�
		data = pkg_data + 6;
		len = data_len;
	}
	else if (data_len == 126) { // ���������ֽڱ�ʾ��С
		mask = pkg_data + 4;  
		data = pkg_data + 8;
		len = (pkg_data[2]) | ((pkg_data[3]) << 8);
	}
	else if (data_len == 127) { // ����˸��ֽڱ�ʾ��С
		mask = pkg_data + 10;
		data = pkg_data + 14;
		len = (pkg_data[2]) | ((pkg_data[3]) << 8) | ((pkg_data[4]) << 16) | ((pkg_data[5]) << 24);
	}

	static char data_buffer[8096];
	unsigned int i;
	for (i = 0; i < len; i++) {
		data_buffer[i] = data[i] ^ mask[i % 4];
	}
	data_buffer[len] = 0;
	printf("recv %s\n", data_buffer);

	char* test_str = "Yes I get";
	ws_send_data(s->to_client_socket, test_str, strlen(test_str));
}

void 
start_server(int port)
{
	init_session_manager(1);
	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (iocp == NULL) {
		goto FAILED;
	}

	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	//1���� 2�� 3����
	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET)
	{
		goto FAILED;
	}
	struct sockaddr_in s_address;
	memset(&s_address, 0, sizeof(s_address));
	s_address.sin_family = AF_INET;
	s_address.sin_addr.S_un.S_addr = inet_addr("0.0.0.0");
	s_address.sin_port = htons(port);

	if (bind(listen_socket, (struct sockaddr *) &s_address, sizeof(s_address)) != 0) {
		goto FAILED;
	}

	if (listen(listen_socket, SOMAXCONN) != 0) {
		goto FAILED;
	}

	//start
	CreateIoCompletionPort((HANDLE)listen_socket, iocp, (DWORD)0, 0);
	do_accept(listen_socket, iocp);
	//end

	//CreateThread(NULL, 0, ServerWorkThread, (LPVOID)iocp, 0, 0);

	DWORD dwTrans;
	struct session* s;
	struct io_package* io_data;
	while (1)
	{
		clear_closed_session();

		//�ȴ�iocp����¼�����
		s = NULL;  //���ص��û�����
		dwTrans = 0;
		io_data = NULL;
		int ret = GetQueuedCompletionStatus(iocp, &dwTrans, (LPDWORD)&s,
			(LPOVERLAPPED*)&io_data, WSA_INFINITE);

		if (ret == 0)
		{
			printf("iocp error\n");
			continue;
		}

		//iocp���ѹ����߳�
		printf("iocp have complete event\n");

		if ( dwTrans == 0 && io_data->opt_type == IOCP_RECV ) //��IOCP_ACCEPT
		{
			close_session(s);
			free(io_data);
			continue;
		}

		switch (io_data->opt_type)
		{
		case IOCP_RECV: {
			io_data->pkg[dwTrans] = 0;
			//printf("IOCP recv %d��%s\n", dwTrans, io_data_ptr->pkg);
			printf("=================\n");
			if (s->is_shake_hand == 0)
			{
				process_ws_http_str(s->to_client_socket, io_data->pkg);
				s->is_shake_hand = 1;
			}
			else if (io_data->pkg[0] == 0x81 || io_data->pkg[0] == 0x82)  //package head�̶�1�ֽ�(1000 0001��1000 0010)
			{
				on_ws_recv_data(s, io_data->pkg, dwTrans);
			}
			else
			{
				// �ر�web socket
				close_session(s);
				free(io_data);
				continue;
			}
			printf("=================\n");
			// ������������ɺ������Ҫ�ټ�һ����������;
			DWORD dwRecv = 0;
			DWORD dwFlags = 0;
			int ret = WSARecv(s->to_client_socket, &(io_data->wsabuffer),
				1, &dwRecv, &dwFlags,
				&(io_data->overlapped), NULL);
		}
		break;

		case IOCP_ACCEPT:
		{
			int client_fd = io_data->accpet_sock;
			int addr_size = (sizeof(struct sockaddr_in) + 16);
			struct sockaddr_in* l_addr = NULL;
			int l_len = sizeof(struct sockaddr_in);

			struct sockaddr_in* r_addr = NULL;
			int r_len = sizeof(struct sockaddr_in);

			GetAcceptExSockaddrs(io_data->wsabuffer.buf,
				0, /*io_data->wsabuffer.len - addr_size * 2, */
				addr_size, addr_size,
				(struct sockaddr**)&l_addr, &l_len,
				(struct sockaddr**)&r_addr, &r_len);

			struct session* s = add_session(client_fd, inet_ntoa(r_addr->sin_addr), ntohs(r_addr->sin_port));
			CreateIoCompletionPort((HANDLE)client_fd, iocp, (DWORD)s, 0);
			do_recv(client_fd, iocp);
			do_accept(listen_socket, iocp);
		}
		break;
		}
	}

FAILED:
	if (iocp != NULL)
	{
		CloseHandle(iocp);
	}

	if (listen_socket != INVALID_SOCKET)
	{
		closesocket(listen_socket);
	}

	WSACleanup();
}
