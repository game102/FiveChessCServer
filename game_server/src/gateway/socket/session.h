#ifndef __SESSION_H__
#define __SESSION_H__

#define MAX_SEND_PKG 2048  //�������� 2K

struct session {
	char client_ip[32];
	int  client_port;
	int  client_socket;
	int  is_removed;
	int  is_shake_hand;

	struct session* next;
	unsigned char send_buf[MAX_SEND_PKG]; //90%���͵������
};

void init_session_manager(int client_socket, int protocal_type);
void exit_session_manager();


// �пͷ��˽������������session;
struct session* save_session(int client_socket, char* client_ip, int client_port);
//�رյ���sesssion
void close_session(struct session* s);

// ��������session�������������session
void foreach_online_session(int(*callback)(struct session* s, void* param), void*param);

// �����ߵ�sesssion���رյ�
void clear_offline_session();
// end 

int
get_socket_type();

int
get_protocal_type();
#endif