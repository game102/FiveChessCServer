#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <WinSock2.h>
#include <Windows.h>
#endif

#include "session.h"
#include "../gateway.h"

#define MAX_SESSION_NUM 6000  //�������6000
#define my_malloc malloc
#define my_free free

struct {
	struct session* online_session;

	struct session* offline_session;
	struct session* offline_session_tail;


	int readed_bit; // ��ǰ�Ѿ���socket�����ȡ������;

	int has_removed;   //�Ƿ���session��remove 
	int socket_type;   // 0 ��ʾTCP socket, 1��ʾ�� websocket
	int protocal_type; // 0 ��ʾ������Э�飬size + ���ݵ�ģʽ
				       // 1,��ʾ�ı�Э�飬�Իس��������ֽ��յ�������Ϊһ����
}session_manager;

int
get_socket_type() {
	return session_manager.socket_type;
}

int
get_proto_type() {
	return session_manager.protocal_type;
}

static struct session* session_alloc() {
	struct session* s = NULL;
	if (session_manager.offline_session_tail != NULL) {
		s = session_manager.offline_session_tail;
		session_manager.offline_session_tail = s->next;
	}
	else { // ����ϵͳ�ĺ��� malloc
		s = my_malloc(sizeof(struct session));
	}
	memset(s, 0, sizeof(struct session));

	return s;
}

static void session_free(struct session* s) {
	// �ж�һ�£��Ǵ�cache�����ȥ�ģ����Ǵ�ϵͳmy_malloc�����ȥ�ģ�
	if (s >= session_manager.offline_session && s < session_manager.offline_session + MAX_SESSION_NUM) {
		s->next = session_manager.offline_session_tail;
		session_manager.offline_session_tail = s;
	}
	else {
		my_free(s);
	}
	// 
}

void init_session_manager(int socket_type, int protocal_type) {
	memset(&session_manager, 0, sizeof(session_manager));

	session_manager.socket_type = socket_type;
	session_manager.protocal_type = protocal_type;

	// ��6000��sessionһ�η��������
	session_manager.offline_session = (struct session*)my_malloc(MAX_SESSION_NUM * sizeof(struct session));
	memset(session_manager.offline_session, 0, MAX_SESSION_NUM * sizeof(struct session));
	// end 

	for (int i = 0; i < MAX_SESSION_NUM; i++) {
		session_manager.offline_session[i].next = session_manager.offline_session_tail;
		session_manager.offline_session_tail = &session_manager.offline_session[i];
	}
}

void exit_session_manager() {

}

struct session* save_session(int client_sock, char* client_ip, int client_port) {
	struct session* s = session_alloc();
	s->client_socket = client_sock;
	s->client_port = client_port;
	int ip_len = strlen(client_ip);
	if (ip_len >= 32) {
		ip_len = 31;
	}
	strncpy(s->client_ip, client_ip, ip_len);
	s->client_ip[ip_len] = 0;

	s->next = session_manager.online_session;
	session_manager.online_session = s;
	return s;
}

void foreach_online_session(int(*callback)(struct session* s, void* param), void*param) {
	if (callback == NULL) {
		return;
	}

	struct session* session_walk = session_manager.online_session;
	while (session_walk) {
		if (session_walk->is_removed == 1) {
			session_walk = session_walk->next;
			continue;
		}
		if (callback(session_walk, param)) {
			return;
		}
		session_walk = session_walk->next;
	}
}

void close_session(struct session* s) {
	s->is_removed = 1;
	session_manager.has_removed = 1;
	printf("client %s:%d exit\n", s->client_ip, s->client_port);
}

extern void
on_connect_lost_entry(struct session* s);

void clear_offline_session() {
	if (session_manager.has_removed == 0) {
		return;
	}

	struct session** session_walk = &session_manager.online_session;
	while (*session_walk) {
		struct session* s = (*session_walk);
		if (s->is_removed) {
			*session_walk = s->next;
			s->next = NULL;

			on_connect_lost_entry(s);
			if (s->client_socket != 0) {
				shutdown(s->client_socket, 2); // �رտͻ��ˣ���ʱ����close���������⡣
				closesocket(s->client_socket);
			}

			// 
			s->client_socket = 0;
			// �ͷ�session
			session_free(s);
		}
		else {
			session_walk = &(*session_walk)->next;
		}
	}
	session_manager.has_removed = 0;
}

static void
ws_send_data(struct session* s, const unsigned char* pkg_data, unsigned int pkg_len) {
	int long_pkg = 0;   //����MAX_SEND_PKG ��̬�ڴ����
	unsigned char* send_buffer = NULL;


	int header = 1; // 0x81
	if (pkg_len <= 125) {
		header++;
	}
	else if (pkg_len <= 0xffff) { //65535
		header += 3;   //���ݳ���2���ֽ�
	}
	else {
		header += 9;   //���ݳ���8���ֽ�
	}

	if (header + pkg_len > MAX_SEND_PKG) {
		long_pkg = 1;
		send_buffer = my_malloc(header + pkg_len);
	}
	else {
		send_buffer = s->send_buf;
	}

	unsigned int send_len;
	//websocket�� �̶��ֽ�  �������ֽ�  mark����  �ֵ�����
	//websocket�� �̶��ֽ�  �������ֽ�  ԭʼ����
	send_buffer[0] = 0x81;

	if (pkg_len <= 125) {
		send_buffer[1] = pkg_len; // ���bitΪ0��
		send_len = 2;
	}
	else if (pkg_len <= 0xffff) {
		send_buffer[1] = 126;
		send_buffer[2] = (pkg_len & 0x000000ff);
		send_buffer[3] = ((pkg_len & 0x0000ff00) >> 8);
		send_len = 4;
	}
	else {  //��֧��
		send_buffer[1] = 127;
		send_buffer[2] = (pkg_len & 0x000000ff);              //11111111
		send_buffer[3] = ((pkg_len & 0x0000ff00) >> 8);       //11111111 00000000
		send_buffer[4] = ((pkg_len & 0x00ff0000) >> 16);      //11111111 00000000 00000000
		send_buffer[5] = ((pkg_len & 0xff000000) >> 24);      //11111111 00000000 00000000 00000000

		send_buffer[6] = 0;   //���ᵽ��ô��λ ��0ռλ
		send_buffer[7] = 0;
		send_buffer[8] = 0;
		send_buffer[9] = 0;
		send_len = 10;  //0-9 10byte
	}
	memcpy(send_buffer + send_len, pkg_data, pkg_len); 
	send_len += pkg_len;
	send(s->client_socket, send_buffer, send_len, 0);
	// 

	if (long_pkg) {
		my_free(send_buffer);
	}
}


static void
tcp_send_data(struct session* s, const unsigned char* body, int len) {
	int long_pkg = 0;
	unsigned char* pkg_ptr = NULL;

	if (len + 2 > MAX_SEND_PKG) {
		pkg_ptr = my_malloc(len + 2);
		long_pkg = 1;
	}
	else {
		pkg_ptr = s->send_buf;
	}

	if (session_manager.protocal_type == JSON_PROTOCAL) {
		//json���ݸ�ʽʹ�õ���json����Э��ķ��(ʹ��\r\n) body+\r\n
		memcpy(pkg_ptr, body, len);   //��ʼλ�ÿ�ʼ�������ɸ��ֽڵ�Ŀ���ڴ��ַ
		strncpy(pkg_ptr + len, "\r\n", 2);   //��src��ָ�ַ���ǰn���ֽڸ��Ƶ�dest��ָ�����У�������dest
		send(s->client_socket, pkg_ptr, len + 2, 0);
	}
	else if (session_manager.protocal_type == BIN_PROTOCAL) {
		//����������Э��ʹ��size + body�ķ��(size Ϊ2���ֽ� size��Ĵ�СΪbody�Ĵ�С + 2)
		memcpy(pkg_ptr + 2, body, len);
		pkg_ptr[0] = ((len + 2)) & 0x000000ff;
		pkg_ptr[1] = (((len + 2)) & 0x0000ff00) >> 8;
		send(s->client_socket, pkg_ptr, len + 2, 0);
	}
	if (long_pkg && pkg_ptr != NULL) {
		my_free(pkg_ptr);
	}
}


void
session_send(struct session* s, unsigned char* body, int len) {

	if (session_manager.socket_type == TCP_SOCKET_IO) {
		tcp_send_data(s, body, len);
	}
	else if (session_manager.socket_type == WEB_SOCKET_IO) {
		ws_send_data(s, body, len);
	}
}

void
session_send_json(struct session* s, json_t* json) {
	char* json_str = NULL;
	json_tree_to_string(json, &json_str);
	session_send(s, json_str, strlen(json_str));
	json_free_str(json_str);
}