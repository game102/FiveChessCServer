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
	GAME_STARTED = 4, // ��Ϸ��ʼ;
	TURN_TO_PLAYER = 5, // �ֵ��ĸ����;
	GIVE_CHESS = 6, // ���������
	CHECKOUT = 7, // ����
	USER_QUIT = 8, // �û��뿪
};
 
enum {
	T_READY = 0, // ��ʱ׼����ʼ��Ϸ; 
	T_STEADY, // ��Ϸ�Ѿ���ʼ�����ǣ���Ҫ��һ�������ʽ��ʼ; �����ͻ��˲�����
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
#define DISK_SIZE 15

struct game_table {
	int status; // ������ǵ�ǰ�����ӵ�״��,��ʼ����TRADEAD��״̬;
	int banker_id; // ���ǵ�ǰ�Ǹ���ҳֺ����¡�
	int cur_turn;
	struct game_seat seats[PLAYER_NUM];

	int chess_disk[DISK_SIZE][DISK_SIZE];  //15��15����
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

static int
check_game_started(struct game_table* table) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (table->seats[i].is_sitdown == 0) {
			return 0;
		}
	}
	return 1;
}

static void
turn_to_player(struct game_table* table, int seatid) {
	table->cur_turn = seatid;
	json_t* json = json_new_comand(FIVE_CHESS_SERVICE, TURN_TO_PLAYER);
	json_object_push_number(json, "2", seatid);
	table_broadcast_json(table, json, -1);
	json_free_value(&json);
}

static void
start_game(struct game_table* table) {
	table->status = T_STEADY; // ������׼�����ˡ�
	table->banker_id = rand() % 2; // [0, 1]���һ���ֺ����е�
	memset(table->chess_disk, 0, sizeof(int) * DISK_SIZE * DISK_SIZE);  //�������

	// �㲥һ����Ϸ��ʼ����������ǵ������ڵĿͻ��ˣ�֪ͨ������ϷҪ��ʼ
	// �ͻ����յ���������Ժ󣬲���ready�Ķ���,����ʽ��ʼ֮ǰ��Ԥ��һ��ʱ��Σ��������ʱ��κ󣬲ſ�ʼ
	json_t* json = json_new_comand(FIVE_CHESS_SERVICE, GAME_STARTED);
	json_object_push_number(json, "2", table->banker_id); // ���͸��ͻ��ˣ��ÿͻ���ȥ�ж��Լ��ǳֺڻ��ǳְ�ɫ;
	table_broadcast_json(table, json, -1); // ���е���Ҷ�Ҫ�յ�����㲥;
	json_free_value(&json);
	// end 

	// �������ֵ���ҿ�ʼ��
	table->status = T_PLAYING;
	turn_to_player(table, table->banker_id); // �ֵ�ׯ�ҿ�ʼ ׯ�ҳֺڣ�
	// end 
}

// 1���Ǳ�ʾ�ֳ�ʤ��, 0���Ǳ�ʾδ�ֳ�ʤ��
static int
checkout_game(struct game_table* table, int seatid) {
	int value = (table->banker_id == seatid) ? BLACK_CHESS : WHITE_CHESS;  //���廹�ǰ���

	// ����ɨ�裬���Ƿ��������������������
	for (int line = 0; line < DISK_SIZE; line++) { // ������
		for (int col = 0; col < DISK_SIZE - 4; col++) { // ������
			if (table->chess_disk[line][col + 0] == value &&
				table->chess_disk[line][col + 1] == value &&
				table->chess_disk[line][col + 2] == value &&
				table->chess_disk[line][col + 3] == value &&
				table->chess_disk[line][col + 4] == value) {
				return 1;
			}
		}
	}
	// end 

	// ����ɨ��
	for (int col = 0; col < DISK_SIZE; col++) {
		for (int line = 0; line < DISK_SIZE - 4; line++) {
			if (table->chess_disk[line + 0][col] == value &&
				table->chess_disk[line + 1][col] == value &&
				table->chess_disk[line + 2][col] == value &&
				table->chess_disk[line + 3][col] == value &&
				table->chess_disk[line + 4][col] == value) {
				return 1;
			}
		}
	}
	// end 

	// ��б��һ��ķ���ʼ 
	// �°벿
	for (int y = 0; y < DISK_SIZE; y++) {
		int start_x = 0;
		int start_y = y;
		while (1) {
			if (start_y < 4 || start_x + 4 >= DISK_SIZE) { // ��ʼ�ĶԽ��߲������
				break;
			}

			if (table->chess_disk[start_y - 0][start_x + 0] == value &&
				table->chess_disk[start_y - 1][start_x + 1] == value &&
				table->chess_disk[start_y - 2][start_x + 2] == value &&
				table->chess_disk[start_y - 3][start_x + 3] == value &&
				table->chess_disk[start_y - 4][start_x + 4] == value) {
				return 1;
			}
			// �Խ��ߵ���һ�����
			start_y--;
			start_x++;

		}
	}

	// �ϰ벿
	for (int x = 1; x < DISK_SIZE - 4; x++) {
		int start_x = x;
		int start_y = DISK_SIZE - 1;

		while (1) {
			if (start_y < 4 || start_x + 4 >= DISK_SIZE) { // ��ʼ�ĶԽ��߲������
				break;
			}

			if (table->chess_disk[start_y - 0][start_x + 0] == value &&
				table->chess_disk[start_y - 1][start_x + 1] == value &&
				table->chess_disk[start_y - 2][start_x + 2] == value &&
				table->chess_disk[start_y - 3][start_x + 3] == value &&
				table->chess_disk[start_y - 4][start_x + 4] == value) {
				return 1;
			}
			// �Խ��ߵ���һ�����
			start_y--;
			start_x++;

		}
	}
	// end 

	// б�ܲ���
	// �Ұ벿��
	for (int y = 0; y < DISK_SIZE; y++) {
		int start_x = DISK_SIZE - 1;
		int start_y = y;
		while (1) {
			if (start_y < 4 || start_x < 4) { // ��ʼ�ĶԽ��߲������
				break;
			}

			if (table->chess_disk[start_y - 0][start_x - 0] == value &&
				table->chess_disk[start_y - 1][start_x - 1] == value &&
				table->chess_disk[start_y - 2][start_x - 2] == value &&
				table->chess_disk[start_y - 3][start_x - 3] == value &&
				table->chess_disk[start_y - 4][start_x - 4] == value) {
				return 1;
			}
			// �Խ��ߵ���һ�����
			start_y--;
			start_x--;

		}
	}

	// ��벿��
	for (int x = DISK_SIZE - 1; x >= 0; x--) {
		int start_x = x;
		int start_y = DISK_SIZE - 1;

		while (1) {
			if (start_y < 4 || start_x < 4) { // ��ʼ�ĶԽ��߲������
				break;
			}

			if (table->chess_disk[start_y - 0][start_x - 0] == value &&
				table->chess_disk[start_y - 1][start_x - 1] == value &&
				table->chess_disk[start_y - 2][start_x - 2] == value &&
				table->chess_disk[start_y - 3][start_x - 3] == value &&
				table->chess_disk[start_y - 4][start_x - 4] == value) {
				return 1;
			}
			// �Խ��ߵ���һ�����
			start_y--;
			start_x--;

		}
	}
	// end 
	return 0;
}

static json_t*
get_user_quit_data(int seatid, int reason) {
	json_t* json = json_new_comand(FIVE_CHESS_SERVICE, USER_QUIT);
	json_object_push_number(json, "2", seatid);
	json_object_push_number(json, "3", reason);

	return json;
}

// �������㲥�����еĿͻ�����ҵ�
// { FIVE_CHESS_SERVICE, USER_QUIT, seadid, reason}
static void do_user_quit(struct game_table* table, int seatid, int reason) {
	if (table->seats[seatid].is_sitdown == 0) {
		return;
	}

	json_t* json = get_user_quit_data(seatid, reason);
	// �㲥�����е���ң����뿪��
	table_broadcast_json(table, json, -1);
	// end 
	json_free_value(&json);

	table->seats[seatid].is_sitdown = 0;
	table->seats[seatid].player_session = NULL;
}

static void
send_checkout(struct game_table* table, int winner) {
	table->status = T_CHECKOUT;

	json_t* json = json_new_comand(FIVE_CHESS_SERVICE, CHECKOUT);
	json_object_push_number(json, "2", winner);
	table_broadcast_json(table, json, -1);
	json_free_value(&json);

	do_user_quit(table, 0, GAME_OVER);
	do_user_quit(table, 1, GAME_OVER);

	table->status = T_READY;
}

static void
on_player_give_chess(struct game_table* table, struct session* s, json_t* json, int len) {
	if (len != 4) { // 0: 2, , 1: 6,  2 : block_x, 3 : block_y,
		write_error_command(s, GIVE_CHESS, INVALID_PARAMS);
		return;
	}

	// ���ӵ�״̬
	if (table->status != T_PLAYING) {
		write_error_command(s, GIVE_CHESS, INVALID_ACTION);
		return;
	}
	// end 

	// ����Ƿ�����������
	if (!is_in_table(table, s)) {
		write_error_command(s, GIVE_CHESS, USER_IS_INTABLE);
		return;
	}
	// end 
	int seatid = get_seatid(table, s);
	if (seatid != table->cur_turn) {// ��ǰ�Ƿ��ֵ��˸����˵��
		write_error_command(s, GIVE_CHESS, INVALID_ACTION);
		return;
	}
	// end 

	// ���������Ҵ������ǵ������λ�õ�����;
	json_t* value = json_object_at(json, "2");
	if (!value || value->type != JSON_NUMBER) {
		write_error_command(s, GIVE_CHESS, INVALID_PARAMS);
		return;
	}
	int x_block = atoi(value->text);

	value = json_object_at(json, "3");
	if (!value || value->type != JSON_NUMBER) {
		write_error_command(s, GIVE_CHESS, INVALID_PARAMS);
		return;
	}
	int y_block = atoi(value->text);

	// ����û�������λ�õĺϷ���
	if (x_block < 0 || x_block >= DISK_SIZE || y_block < 0 || y_block >= DISK_SIZE) {
		write_error_command(s, GIVE_CHESS, INVALID_PARAMS);
		return;
	}
	// end

	// �������ط���û���Ѿ����¹��壬����У�Ҳ�ǷǷ��Ĳ���
	if (table->chess_disk[y_block][x_block] != 0) {
		write_error_command(s, GIVE_CHESS, INVALID_PARAMS);
		return;
	}
	// end 

	// ������ȷ��
	if (seatid == table->banker_id) { // ��ɫ
		table->chess_disk[y_block][x_block] = BLACK_CHESS; // 
	}
	else { // ��ɫ
		table->chess_disk[y_block][x_block] = WHITE_CHESS;  
	}

	// ������û������㲥��ȥ��
	json_t* send_data = json_new_comand(FIVE_CHESS_SERVICE, GIVE_CHESS);
	json_object_push_number(send_data, "2", OK);
	json_object_push_number(send_data, "3", seatid);
	json_object_push_number(send_data, "4", x_block);
	json_object_push_number(send_data, "5", y_block);
	table_broadcast_json(table, send_data, -1); // �㲥�������������е���
	json_free_value(&send_data);
	// end 

	// ����
	if (checkout_game(table, seatid)) { // ��Ϸ�������̣����ͽ�����
		send_checkout(table, seatid);
		return;
	}
	// end 

	// ת����һ�����
	int next = (table->cur_turn + 1);
	if (next >= PLAYER_NUM) {
		next = 0;
	}
	turn_to_player(table, next);
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

	// �жϵ�ǰ���ӵ�״̬�������������Ϸ�У���ô���Ǿ�ֱ�Ӳ��������ˡ�
	if (table->status != T_READY) {
		write_error_command(s, SITDOWN, INAVLID_TABLE_STATUS);
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

	// �����Ϸ�Ƿ�ʼ
	if (check_game_started(table)) {
		start_game(table);
	}
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

	if (table->status == T_PLAYING) {
		// ����
		table->status = T_CHECKOUT;
		int winner = seatid + 1;
		if (winner >= PLAYER_NUM) {
			winner = 0;
		}
		send_checkout(table, winner);
		// end
	}
	// ������ʼ,��ʳ
	table->status = T_READY;
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
		case GIVE_CHESS:
		{
			on_player_give_chess(table, s, json, size);
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

	if (table->status == T_PLAYING) {
		// ����
		table->status = T_CHECKOUT;
		int winner = seatid + 1;
		if (winner >= PLAYER_NUM) {
			winner = 0;
		}
		send_checkout(table, winner);
		// end
	}

	// ������ʼ
	table->status = T_READY;
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