#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "tcp_session.h"

#define MAX_SESSION_NUM 6000   //��������
#define MAX_RECV_BUFFER 8196   //�������� 8k

#define my_malloc malloc
#define my_free free

static struct 
{
	struct session* online_session;  //Linked List1
	struct session* session_cache;   //Linked List2
	struct session* end_ptr;        //Linked List2

	char receive_buffer[MAX_RECV_BUFFER];
	int already_readed;

	/* 0:������  size+content
   1:�ı� ��/r/n ��Ϊ�������� */
	int protocol_mode;
	int has_removed_session;

}session_manager;


static struct session* session_alloc()
{
	struct session* temp_sesssion = NULL;

	if (session_manager.end_ptr != NULL)
	{
		temp_sesssion = session_manager.end_ptr;
		session_manager.end_ptr = session_manager.end_ptr->next;
	}
	else  //���ӳ��� MAX_SESSION_NUM ����ϵͳ����
	{
		temp_sesssion = my_malloc(sizeof(struct session));
	}
	
	memset(temp_sesssion, 0, sizeof(struct session));
	return temp_sesssion;
}

static void session_free(struct session *s)
{
	if (s >= session_manager.session_cache
		&& s < session_manager.session_cache + MAX_SESSION_NUM)
	{
		s->next = session_manager.end_ptr;
		session_manager.end_ptr = s;
	}
	else
	{
		my_free(s);
	}
}

void init_session_manager(int bin_or_text)
{
	memset(&session_manager, 0, sizeof(session_manager));
	session_manager.protocol_mode = bin_or_text;

	session_manager.session_cache = my_malloc(MAX_SESSION_NUM * sizeof(struct session));
	memset(session_manager.session_cache, 0, MAX_SESSION_NUM * sizeof(struct session));

	for (int i = 0; i < MAX_SESSION_NUM; i++)
	{
		session_manager.session_cache[i].next = session_manager.end_ptr;
		session_manager.end_ptr = &session_manager.session_cache[i];
	}
}

void exit_session_manager()
{

}

struct session* add_session(int to_client_socket, char* ip, int port)
{
	struct session* s = session_alloc();
	s->to_client_socket = to_client_socket;
	s->to_client_port = port;
	
	int len = strlen(ip);  //192.168.0.1
	if (len >= 32)
	{
		len = 31;
	}

	strncpy(s->to_client_ip, ip, len);
	s->to_client_ip[len] = 0;

	s->next = session_manager.online_session;
	session_manager.online_session = s;

	return s;
}

void foreach_online_session(int(*callback)(struct session *s, void *p), void *p)
{
	if (callback == NULL)
	{
		return;
	}

	struct session *walk = session_manager.online_session;

	while (walk)
	{
		if (walk->is_removed == 1)
		{
			walk = walk->next;
			continue;
		}

		if (callback(walk, p))  //����Ϊ����������� ,�����ҵ�session��Ҫbreak 
		{
			return;
		}

		walk = walk->next;
	}
}

void close_session(struct session *s)
{
	s->is_removed = 1;
	session_manager.has_removed_session = 1;
	printf("client %s:%d exit\n", s->to_client_ip, s->to_client_port);
}

static void text_process_package(struct session *s)
{

}

static void bin_process_package(struct session* s)
{
	if (session_manager.already_readed < 4)  //ǰ4��byteΪ������(head+body).length
	{
		return;
	}

	//�����Ʋ��
	int *pack_head = (int*)session_manager.receive_buffer;
	int pack_len = (*pack_head);   //(head+body).length

	if (pack_len > MAX_RECV_BUFFER)
	{
		goto pack_failed;
	}

	//���������
	int handle_total = 0;  //�Ѿ�����İ�
	while (session_manager.already_readed >= pack_len)
	{
		//��ǰ������

#pragma region �ƶ�����һ����
		handle_total += pack_len;

		if (session_manager.already_readed - handle_total < 4)  //head��û����
		{
			if (session_manager.already_readed > handle_total)
			{
				//�յ��İ�ǰ��-�����ڴ�����,��dest��ָ�ĵ�ַ��,dest source num
				memmove(session_manager.receive_buffer,
					session_manager.receive_buffer + handle_total,
					session_manager.already_readed - handle_total);
			}
			session_manager.already_readed -= handle_total;
			return;
		}

		//�����Ʋ��
		pack_head = (int*)(handle_total + session_manager.receive_buffer);
		pack_len = (*pack_head);   //head+body
		if (pack_len > MAX_RECV_BUFFER)
		{
			goto pack_failed;
		}

		if ((session_manager.already_readed - handle_total) < pack_len)  //body��û����
		{
			//�յ��İ�ǰ��-�����ڴ�����,��dest��ָ�ĵ�ַ�ϡ�
			memmove(session_manager.receive_buffer,
				session_manager.receive_buffer + handle_total,
				session_manager.already_readed - handle_total);

			session_manager.already_readed -= handle_total;
			return;
		}
#pragma endregion
	}

	return;
pack_failed:
	close_session(s);
}


void session_data_receivesession_data_receive(struct session *s)
{
	int readed = recv(s->to_client_socket,
		session_manager.receive_buffer + session_manager.already_readed,
		MAX_RECV_BUFFER - session_manager.already_readed, 0);

	if (readed <= 0)  //�Զ˹ر� TcpЭ�飬�����Ͽ��Ļ��������Receive�᷵��0��
	{
		close_session(s);
		return;
	}

	session_manager.already_readed += readed;

	if (session_manager.protocol_mode == 0)
	{
		bin_process_package(s);
	}
	else
	{
		text_process_package(s);
	}

}

void clear_closed_session()
{
	if (session_manager.has_removed_session = 0)
	{
		return;
	}

	struct session **walk = &session_manager.online_session;

	while (*walk)
	{
		struct session *s = (*walk);
		if (s->is_removed)
		{
			*walk = s->next;
			s->next = NULL;
			closesocket(s->to_client_socket);
			s->to_client_socket = 0;
			session_free(s);
		}
		else
		{
			walk = &(*walk)->next;
		}
	}

	session_manager.has_removed_session = 0;
}


/*
��ʽ1 �ı�Э��json xml ...
��ʽ2 ������Э��
*/