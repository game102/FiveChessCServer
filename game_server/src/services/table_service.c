#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "service_type.h"
#include "table_service.h"

enum {
	OK = 1, // �ɹ���
	INVALID_COMMAND = -100, // ��Ч�Ĳ�������
	INVALID_PARAMS = -101, // ��Ч���û�����;
	USER_IS_INTABLE = -102, // ����Ѿ�������������ظ������;
	TABLE_IS_FULL = -103, // ��ǰ�������������Ժ�����;
	USER_IS_NOT_INTABLE = -104, //��ǰ����Ҳ�����������
	INAVLID_TABLE_STATUS = -105, // ���������״̬;
	INVALID_ACTION = -106, // �Ƿ�����
};

enum {
	BLACK_CHESS = 1, // ��ʾ���Ǻ�ɫ������;
	WHITE_CHESS = 2, // ��ɫ���ǰ�ɫ������;
};

enum {
	SITDOWN = 1, // �����������
	USER_ARRIVED = 2, // ����ҽ���; 
	STANDUP = 3, // ���վ��;
	GAME_STATED = 4, // ��Ϸ��ʼ;
	TURN_TO_PLAYER = 5, // �ֵ��ĸ����;
	GIVE_CHESS = 6, // ���������
	CHECKOUT = 7, // ����
	USER_QUIT = 8, // �û��뿪
};
 
enum {
	T_READY = 0, // ��ʱ׼����ʼ��Ϸ; 
	T_STEADY, // ��Ϸ�Ѿ���ʼ�����ǣ���Ҫ��һ�������ʽ��ʼ;
	T_PLAYING, // ��Ϸ���ڽ�����;
	T_CHECKOUT, // ��Ϸ���ڽ�����;
};

// ����߳���ԭ��
enum {
	GAME_OVER = 1,
	PLAY_LOST = 2,
	PLAY_STANDUP = 3, // �뿪,����
};
// end 

// ����session���������,û�����������ôһ˵
typedef struct session game_player;

struct game_seat {
	int is_sitdown; // �Ƿ�Ϊһ����Ч������;
	int seat_id; // ��λ��id��
	// ��ҵĶ���
	game_player* player_session;
};


// �����������
#define PLAYER_NUM 2
struct game_table {
	int status;
	struct game_seat seats[PLAYER_NUM];
};

static void
write_error_command(struct session*s, int opt, int status) {
	json_t* json = json_new_comand(FIVE_CHESS_SERVICE, opt);
	json_object_push_number(json, "2", status);
	session_send_json(s, json);
	json_free_value(&json);
}

static void
table_broadcast_json(struct game_table* table, json_t* json, int not_to_seatid) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (table->seats[i].is_sitdown && i != not_to_seatid) {
			session_send_json(table->seats[i].player_session, json);
		}
	}
}

static int
is_in_table(struct game_table* table, game_player* p_session) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (table->seats[i].is_sitdown && table->seats[i].player_session == p_session) {
			return 1;
		}
	}
	return 0;
}


static int
get_seatid(struct game_table* table, game_player* p_session) {  //player��table��i��seat����
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (table->seats[i].is_sitdown && table->seats[i].player_session == p_session) {
			return i;
		}
	}
	return -1;
}

static int
get_empty_seatid(struct game_table* table, game_player* p_session) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (table->seats[i].is_sitdown == 0) {
			return i;
		}
	}
	return -1;
}


static json_t*
get_user_arrived_data(struct game_table* table, int seatid) {
	json_t* json = json_new_comand(FIVE_CHESS_SERVICE, USER_ARRIVED);
	// ���������������ͻ��˵ģ�����Ҫдstatus��;
	// ��ҵ����ݣ�����û����ҵ����ݣ�����������seatid������;
	json_object_push_number(json, "2", seatid);
	return json;
}

static void
sitdown_success(struct game_table* table, game_player* p_session, int seatid) {
	// Ҫ��������ҵ����ݷ��͸������� {2 seatid}
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (i != seatid && table->seats[i].is_sitdown == 1) {
			json_t* json = get_user_arrived_data(table, i);
			session_send_json(p_session, json);
			json_free_value(&json);
		}
	}
	// end 
}

static void
on_player_sitdown(struct game_table* table, struct session* s, json_t* json, int len) {
	if (len != 2) { // �����Э�飬�����ϸ�����
		write_error_command(s, SITDOWN, INVALID_PARAMS);
		return;
	}

	// �ж�һ������Ƿ��Ѿ�������������
	if (is_in_table(table, s)) {
		write_error_command(s, SITDOWN, USER_IS_INTABLE);
		return;
	}
	// end 

	// �������ҳ������Ҹ���λ
	int seatid = get_empty_seatid(table, s);
	if (seatid < 0 || seatid >= PLAYER_NUM) {
		write_error_command(s, SITDOWN, TABLE_IS_FULL);
		return;
	}
	// end 

	// �������ݵ���λ
	table->seats[seatid].player_session = s;
	table->seats[seatid].seat_id = seatid;
	table->seats[seatid].is_sitdown = 1;
	// end 

	// ���͸��ͻ��ˣ���ʾ�����³ɹ��ˡ�
	// {stype, opt_cmd, status, seatid};
	json_t* response = json_new_comand(FIVE_CHESS_SERVICE, SITDOWN);
	json_object_push_number(response, "2", OK);
	json_object_push_number(response, "3", seatid);
	session_send_json(s, response);
	json_free_value(&response);
	// end 

	// ������������ҵ����ݸ���ǰ���µ����;
	sitdown_success(table, s, seatid);
	// end 

	// Ҫ�㲥���������������������ң� �������ҽ����ˣ�
	json_t* u_arrived = get_user_arrived_data(table, seatid);
	//  �㲥��������ӵ��������,��Ҫ�ٹ㲥�����Լ��ˡ�
	table_broadcast_json(table, u_arrived, seatid);
	// 
	json_free_value(&u_arrived);
	// end 
}

static void
on_player_standup(struct game_table* table, struct session* s, json_t* json, int len) {
	if (len < 2) {
		write_error_command(s, STANDUP, INVALID_PARAMS);
		return;
	}

	// ����Ƿ�����������
	if (!is_in_table(table, s)) {
		write_error_command(s, STANDUP, USER_IS_NOT_INTABLE);
		return;
	}
	// end 

	// �������Ϸ�У�����...
	// end 

	int seatid = get_seatid(table, s);
	table->seats[seatid].is_sitdown = 0; // ��λ
	table->seats[seatid].player_session = NULL;
	table->seats[seatid].seat_id = -1;

	// ����뿪�ˣ�����Ҫ�������Ϣ�㲥����������ң�Ҳ�����Լ�
	json_t* standup_data = json_new_comand(FIVE_CHESS_SERVICE, STANDUP);
	json_object_push_number(standup_data, "2", OK);
	json_object_push_number(standup_data, "3", seatid);
	session_send_json(s, standup_data);
	table_broadcast_json(table, standup_data, -1); // �㲥�����е��ˡ�
	// end 
}

static void
init_service_module(struct service_module* module) {
	struct game_table* table = malloc(sizeof(struct game_table));
	memset(table, 0, sizeof(struct game_table));
	module->module_data = table;
}


static int
on_five_chess_cmd(void* module_data, struct session* s,
	json_t* json, unsigned char* data, int len) {
	struct game_table* table = (struct game_table*)module_data;
	int size = json_object_size(json);
	if (size < 2) { // ֱ�ӷ��� key��value
		return 0;
	}

	json_t* j_opt_cmd = json_object_at(json, "1");
	if (j_opt_cmd == NULL || j_opt_cmd->type != JSON_NUMBER) {
		return 0;
	}
	int opt_cmd = atoi(j_opt_cmd->text);

	switch (opt_cmd) {
	case SITDOWN:
	{
		on_player_sitdown(table, s, json, size);
	}
	break;
	case STANDUP:
	{
		on_player_standup(table, s, json, size);
	}
	break;

	default: // ��Ч�Ĳ���
	{
		write_error_command(s, opt_cmd, INVALID_COMMAND);
	}
	break;
	}
	return 0;
}

static void
on_player_lost(void* module_data, struct session* s) {
	struct game_table* table = (struct game_table*)module_data;
	int seatid = get_seatid(table, s);
	if (seatid < 0 || seatid >= PLAYER_NUM) { // �������������;
		return;
	}

	// ����뿪
	table->seats[seatid].player_session = NULL;
	table->seats[seatid].is_sitdown = 0;
	table->seats[seatid].seat_id = -1;
	// end 

	// �㲥
	// ����뿪�ˣ�����Ҫ�������Ϣ�㲥�����������
	json_t* standup_data = json_new_comand(FIVE_CHESS_SERVICE, STANDUP);
	json_object_push_number(standup_data, "2", OK);
	json_object_push_number(standup_data, "3", seatid);
	table_broadcast_json(table, standup_data, -1); // �㲥�����е��ˡ�
	// end 
	return;
}


struct service_module SERVICE_TABLE = {
	FIVE_CHESS_SERVICE,
	init_service_module, // ע����ɺ�ĳ�ʼ�� *init_service_module
	NULL, // ����������Э��
	on_five_chess_cmd, // json����Э�� *on_json_protocal_recv
	on_player_lost, // ���Ӷ�ʧ *on_connect_lost
	NULL, // �û�����
};