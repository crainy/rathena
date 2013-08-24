// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/socket.h"
#include "../common/sql.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "inter.h"
#include "char.h"
#include "char_mapif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mapif_sendall(unsigned char *buf, unsigned int len)
{
	int i, c;

	c = 0;
	for(i = 0; i < ARRAYLENGTH(server); i++) {
		int fd;
		if ((fd = server[i].fd) > 0) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
			c++;
		}
	}

	return c;
}

int mapif_sendallwos(int sfd, unsigned char *buf, unsigned int len)
{
	int i, c;

	c = 0;
	for(i = 0; i < ARRAYLENGTH(server); i++) {
		int fd;
		if ((fd = server[i].fd) > 0 && fd != sfd) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
			c++;
		}
	}

	return c;
}

int mapif_send(int fd, unsigned char *buf, unsigned int len)
{
	if (fd >= 0) {
		int i;
		ARR_FIND( 0, ARRAYLENGTH(server), i, fd == server[i].fd );
		if( i < ARRAYLENGTH(server) )
		{
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
			return 1;
		}
	}
	return 0;
}



// Send map-servers the fame ranking lists
int char_send_fame_list(int fd)
{
	int i, len = 8;
	unsigned char buf[32000];

	WBUFW(buf,0) = 0x2b1b;

	for(i = 0; i < fame_list_size_smith && smith_fame_list[i].id; i++) {
		memcpy(WBUFP(buf, len), &smith_fame_list[i], sizeof(struct fame_list));
		len += sizeof(struct fame_list);
	}
	// add blacksmith's block length
	WBUFW(buf, 6) = len;

	for(i = 0; i < fame_list_size_chemist && chemist_fame_list[i].id; i++) {
		memcpy(WBUFP(buf, len), &chemist_fame_list[i], sizeof(struct fame_list));
		len += sizeof(struct fame_list);
	}
	// add alchemist's block length
	WBUFW(buf, 4) = len;

	for(i = 0; i < fame_list_size_taekwon && taekwon_fame_list[i].id; i++) {
		memcpy(WBUFP(buf, len), &taekwon_fame_list[i], sizeof(struct fame_list));
		len += sizeof(struct fame_list);
	}
	// add total packet length
	WBUFW(buf, 2) = len;

	if (fd != -1)
		mapif_send(fd, buf, len);
	else
		mapif_sendall(buf, len);

	return 0;
}

void char_update_fame_list(int type, int index, int fame)
{
	unsigned char buf[8];
	WBUFW(buf,0) = 0x2b22;
	WBUFB(buf,2) = type;
	WBUFB(buf,3) = index;
	WBUFL(buf,4) = fame;
	mapif_sendall(buf, 8);
}

void char_sendall_playercount(int users){
	uint8 buf[6];
	// send number of players to all map-servers
	WBUFW(buf,0) = 0x2b00;
	WBUFL(buf,2) = users;
	mapif_sendall(buf,6);
}

int char_parsemap_getmapname(int fd, int id){
	int j = 0, i = 0;
	if (RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2))
		return 0;

	memset(server[id].map, 0, sizeof(server[id].map));
	for(i = 4; i < RFIFOW(fd,2); i += 4) {
		server[id].map[j] = RFIFOW(fd,i);
		j++;
	}

	ShowStatus("Map-Server %d connected: %d maps, from IP %d.%d.%d.%d port %d.\n",
				id, j, CONVIP(server[id].ip), server[id].port);
	ShowStatus("Map-server %d loading complete.\n", id);

	// send name for wisp to player
	WFIFOHEAD(fd, 3 + NAME_LENGTH);
	WFIFOW(fd,0) = 0x2afb;
	WFIFOB(fd,2) = 0;
	memcpy(WFIFOP(fd,3), charserv_config.wisp_server_name, NAME_LENGTH);
	WFIFOSET(fd,3+NAME_LENGTH);

	char_send_fame_list(fd); //Send fame list.

	{
		unsigned char buf[16384];
		int x;
		if (j == 0) {
			ShowWarning("Map-server %d has NO maps.\n", id);
		} else {
			// Transmitting maps information to the other map-servers
			WBUFW(buf,0) = 0x2b04;
			WBUFW(buf,2) = j * 4 + 10;
			WBUFL(buf,4) = htonl(server[id].ip);
			WBUFW(buf,8) = htons(server[id].port);
			memcpy(WBUFP(buf,10), RFIFOP(fd,4), j * 4);
			mapif_sendallwos(fd, buf, WBUFW(buf,2));
		}
		// Transmitting the maps of the other map-servers to the new map-server
		for(x = 0; x < ARRAYLENGTH(server); x++) {
			if (server[x].fd > 0 && x != id) {
				WFIFOHEAD(fd,10 +4*ARRAYLENGTH(server[x].map));
				WFIFOW(fd,0) = 0x2b04;
				WFIFOL(fd,4) = htonl(server[x].ip);
				WFIFOW(fd,8) = htons(server[x].port);
				j = 0;
				for(i = 0; i < ARRAYLENGTH(server[x].map); i++)
					if (server[x].map[i])
						WFIFOW(fd,10+(j++)*4) = server[x].map[i];
				if (j > 0) {
					WFIFOW(fd,2) = j * 4 + 10;
					WFIFOSET(fd,WFIFOW(fd,2));
				}
			}
		}
	}
	RFIFOSKIP(fd,RFIFOW(fd,2));
	return 1;
}

int char_parsemap_askscdata(int fd, int id){
	if (RFIFOREST(fd) < 10)
		return 0;
	{
#ifdef ENABLE_SC_SAVING
		int aid, cid;
		aid = RFIFOL(fd,2);
		cid = RFIFOL(fd,6);
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT type, tick, val1, val2, val3, val4 from `%s` WHERE `account_id` = '%d' AND `char_id`='%d'",
			schema_config.scdata_db, aid, cid) )
		{
			Sql_ShowDebug(sql_handle);
			return 0;
		}
		if( Sql_NumRows(sql_handle) > 0 )
		{
			struct status_change_data scdata;
			int count;
			char* data;

			WFIFOHEAD(fd,14+50*sizeof(struct status_change_data));
			WFIFOW(fd,0) = 0x2b1d;
			WFIFOL(fd,4) = aid;
			WFIFOL(fd,8) = cid;
			for( count = 0; count < 50 && SQL_SUCCESS == Sql_NextRow(sql_handle); ++count )
			{
				Sql_GetData(sql_handle, 0, &data, NULL); scdata.type = atoi(data);
				Sql_GetData(sql_handle, 1, &data, NULL); scdata.tick = atoi(data);
				Sql_GetData(sql_handle, 2, &data, NULL); scdata.val1 = atoi(data);
				Sql_GetData(sql_handle, 3, &data, NULL); scdata.val2 = atoi(data);
				Sql_GetData(sql_handle, 4, &data, NULL); scdata.val3 = atoi(data);
				Sql_GetData(sql_handle, 5, &data, NULL); scdata.val4 = atoi(data);
				memcpy(WFIFOP(fd, 14+count*sizeof(struct status_change_data)), &scdata, sizeof(struct status_change_data));
			}
			if (count >= 50)
				ShowWarning("Too many status changes for %d:%d, some of them were not loaded.\n", aid, cid);
			if (count > 0)
			{
				WFIFOW(fd,2) = 14 + count*sizeof(struct status_change_data);
				WFIFOW(fd,12) = count;
				WFIFOSET(fd,WFIFOW(fd,2));

				//Clear the data once loaded.
				if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id` = '%d' AND `char_id`='%d'", schema_config.scdata_db, aid, cid) )
					Sql_ShowDebug(sql_handle);
			}
		}
		Sql_FreeResult(sql_handle);
#endif
		RFIFOSKIP(fd, 10);
	}
	return 1;
}

int char_parsemap_getusercount(int fd, int id){
	if (RFIFOREST(fd) < 4)
		return 0;
	if (RFIFOW(fd,2) != server[id].users) {
		server[id].users = RFIFOW(fd,2);
		ShowInfo("User Count: %d (Server: %d)\n", server[id].users, id);
	}
	RFIFOSKIP(fd, 4);
	return 1;
}

int char_parsemap_regmapuser(int fd, int id){
	int i;
	if (RFIFOREST(fd) < 6 || RFIFOREST(fd) < RFIFOW(fd,2))
		return 0;
	{
		//TODO: When data mismatches memory, update guild/party online/offline states.
		int aid, cid;
		struct online_char_data* character;
		DBMap* online_char_db = char_get_onlinedb();

		server[id].users = RFIFOW(fd,4);
		online_char_db->foreach(online_char_db,char_db_setoffline,id); //Set all chars from this server as 'unknown'
		for(i = 0; i < server[id].users; i++) {
			aid = RFIFOL(fd,6+i*8);
			cid = RFIFOL(fd,6+i*8+4);
			character = idb_ensure(online_char_db, aid, create_online_char_data);
			if( character->server > -1 && character->server != id )
			{
				ShowNotice("Set map user: Character (%d:%d) marked on map server %d, but map server %d claims to have (%d:%d) online!\n",
					character->account_id, character->char_id, character->server, id, aid, cid);
				mapif_disconnectplayer(server[character->server].fd, character->account_id, character->char_id, 2);
			}
			character->server = id;
			character->char_id = cid;
		}
		//If any chars remain in -2, they will be cleaned in the cleanup timer.
		RFIFOSKIP(fd,RFIFOW(fd,2));
	}
	return 1;
}

int char_parsemap_reqsavechar(int fd, int id){
	if (RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2))
			return 0;
	{
		int aid = RFIFOL(fd,4), cid = RFIFOL(fd,8), size = RFIFOW(fd,2);
		struct online_char_data* character;
		DBMap* online_char_db = char_get_onlinedb();

		if (size - 13 != sizeof(struct mmo_charstatus))
		{
			ShowError("parse_from_map (save-char): Size mismatch! %d != %d\n", size-13, sizeof(struct mmo_charstatus));
			RFIFOSKIP(fd,size);
			return 0;
		}
		//Check account only if this ain't final save. Final-save goes through because of the char-map reconnect
		if (RFIFOB(fd,12) || RFIFOB(fd,13) || (
			(character = (struct online_char_data*)idb_get(online_char_db, aid)) != NULL &&
			character->char_id == cid))
		{
			struct mmo_charstatus char_dat;
			memcpy(&char_dat, RFIFOP(fd,13), sizeof(struct mmo_charstatus));
			mmo_char_tosql(cid, &char_dat);
		} else {	//This may be valid on char-server reconnection, when re-sending characters that already logged off.
			ShowError("parse_from_map (save-char): Received data for non-existant/offline character (%d:%d).\n", aid, cid);
			set_char_online(id, cid, aid);
		}

		if (RFIFOB(fd,12))
		{	//Flag, set character offline after saving. [Skotlex]
			set_char_offline(cid, aid);
			WFIFOHEAD(fd,10);
			WFIFOW(fd,0) = 0x2b21; //Save ack only needed on final save.
			WFIFOL(fd,2) = aid;
			WFIFOL(fd,6) = cid;
			WFIFOSET(fd,10);
		}
		RFIFOSKIP(fd,size);
	}
	return 1;
}

int char_parsemap_authok(int fd, int id){
	if( RFIFOREST(fd) < 19 )
		return 0;
	else{
		int account_id = RFIFOL(fd,2);
		uint32 login_id1 = RFIFOL(fd,6);
		uint32 login_id2 = RFIFOL(fd,10);
		uint32 ip = RFIFOL(fd,14);
		int version = RFIFOB(fd,18);
		RFIFOSKIP(fd,19);

		if( runflag != CHARSERVER_ST_RUNNING ){
			WFIFOHEAD(fd,7);
			WFIFOW(fd,0) = 0x2b03;
			WFIFOL(fd,2) = account_id;
			WFIFOB(fd,6) = 0;// not ok
			WFIFOSET(fd,7);
		}else{
			struct auth_node* node;
			DBMap*  auth_db = char_get_authdb();
			DBMap* online_char_db = char_get_onlinedb();

			// create temporary auth entry
			CREATE(node, struct auth_node, 1);
			node->account_id = account_id;
			node->char_id = 0;
			node->login_id1 = login_id1;
			node->login_id2 = login_id2;
			//node->sex = 0;
			node->ip = ntohl(ip);
			//node->expiration_time = 0; // unlimited/unknown time by default (not display in map-server)
			//node->gmlevel = 0;
			idb_put(auth_db, account_id, node);

			//Set char to "@ char select" in online db [Kevin]
			set_char_charselect(account_id);
			{
				struct online_char_data* character = (struct online_char_data*)idb_get(online_char_db, account_id);
				if( character != NULL ){
					character->pincode_success = true;
					character->version = version;
				}
			}

			WFIFOHEAD(fd,7);
			WFIFOW(fd,0) = 0x2b03;
			WFIFOL(fd,2) = account_id;
			WFIFOB(fd,6) = 1;// ok
			WFIFOSET(fd,7);
		}
	}
	return 1;
}

int char_parsemap_reqchangemapserv(int fd, int id){
	if (RFIFOREST(fd) < 39)
		return 0;
	{
		int map_id, map_fd = -1;
		struct mmo_charstatus* char_data;
		struct mmo_charstatus char_dat;
		DBMap* char_db_ = char_get_chardb();

		map_id = search_mapserver(RFIFOW(fd,18), ntohl(RFIFOL(fd,24)), ntohs(RFIFOW(fd,28))); //Locate mapserver by ip and port.
		if (map_id >= 0)
			map_fd = server[map_id].fd;
		//Char should just had been saved before this packet, so this should be safe. [Skotlex]
		char_data = (struct mmo_charstatus*)uidb_get(char_db_,RFIFOL(fd,14));
		if (char_data == NULL) {	//Really shouldn't happen.
			mmo_char_fromsql(RFIFOL(fd,14), &char_dat, true);
			char_data = (struct mmo_charstatus*)uidb_get(char_db_,RFIFOL(fd,14));
		}

		if( runflag == CHARSERVER_ST_RUNNING &&
			session_isActive(map_fd) &&
			char_data )
		{	//Send the map server the auth of this player.
			struct online_char_data* data;
			struct auth_node* node;
			DBMap*  auth_db = char_get_authdb();
			DBMap* online_char_db = char_get_onlinedb();

			int aid = RFIFOL(fd,2);

			//Update the "last map" as this is where the player must be spawned on the new map server.
			char_data->last_point.map = RFIFOW(fd,18);
			char_data->last_point.x = RFIFOW(fd,20);
			char_data->last_point.y = RFIFOW(fd,22);
			char_data->sex = RFIFOB(fd,30);

			// create temporary auth entry
			CREATE(node, struct auth_node, 1);
			node->account_id = aid;
			node->char_id = RFIFOL(fd,14);
			node->login_id1 = RFIFOL(fd,6);
			node->login_id2 = RFIFOL(fd,10);
			node->sex = RFIFOB(fd,30);
			node->expiration_time = 0; // FIXME (this thing isn't really supported we could as well purge it instead of fixing)
			node->ip = ntohl(RFIFOL(fd,31));
			node->group_id = RFIFOL(fd,35);
			node->changing_mapservers = 1;
			idb_put(auth_db, aid, node);

			data = idb_ensure(online_char_db, aid, create_online_char_data);
			data->char_id = char_data->char_id;
			data->server = map_id; //Update server where char is.

			//Reply with an ack.
			WFIFOHEAD(fd,30);
			WFIFOW(fd,0) = 0x2b06;
			memcpy(WFIFOP(fd,2), RFIFOP(fd,2), 28);
			WFIFOSET(fd,30);
		} else { //Reply with nak
			WFIFOHEAD(fd,30);
			WFIFOW(fd,0) = 0x2b06;
			memcpy(WFIFOP(fd,2), RFIFOP(fd,2), 28);
			WFIFOL(fd,6) = 0; //Set login1 to 0.
			WFIFOSET(fd,30);
		}
		RFIFOSKIP(fd,39);
	}
	return 1;
}

int char_parsemap_askrmfriend(int fd, int id){
	if (RFIFOREST(fd) < 10)
		return 0;
	{
		int char_id, friend_id;
		char_id = RFIFOL(fd,2);
		friend_id = RFIFOL(fd,6);
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d' AND `friend_id`='%d' LIMIT 1",
			schema_config.friend_db, char_id, friend_id) ) {
			Sql_ShowDebug(sql_handle);
			return 0;
		}
		RFIFOSKIP(fd,10);
	}
	return 1;
}

int char_parsemap_reqcharname(int fd, int id){
	if (RFIFOREST(fd) < 6)
		return 0;

	WFIFOHEAD(fd,30);
	WFIFOW(fd,0) = 0x2b09;
	WFIFOL(fd,2) = RFIFOL(fd,2);
	char_loadName((int)RFIFOL(fd,2), (char*)WFIFOP(fd,6));
	WFIFOSET(fd,30);

	RFIFOSKIP(fd,6);
	return 1;
}

/*
 * Forward email update request to loginserv
 */
int char_parsemap_reqnewemail(int fd, int id){
	if (RFIFOREST(fd) < 86)
		return 0;
	if (login_fd > 0) { // don't send request if no login-server
		WFIFOHEAD(login_fd,86);
		memcpy(WFIFOP(login_fd,0), RFIFOP(fd,0),86); // 0x2722 <account_id>.L <actual_e-mail>.40B <new_e-mail>.40B
		WFIFOW(login_fd,0) = 0x2722;
		WFIFOSET(login_fd,86);
	}
	RFIFOSKIP(fd, 86);
	return 1;
}

int char_parsemap_fwlog_changestatus(int fd, int id){
	if (RFIFOREST(fd) < 44)
		return 0;
	{
		int result = 0; // 0-login-server request done, 1-player not found, 2-gm level too low, 3-login-server offline
		char esc_name[NAME_LENGTH*2+1];

		int acc = RFIFOL(fd,2); // account_id of who ask (-1 if server itself made this request)
		const char* name = (char*)RFIFOP(fd,6); // name of the target character
		int type = RFIFOW(fd,30); // type of operation: 1-block, 2-ban, 3-unblock, 4-unban
		short year = RFIFOW(fd,32);
		short month = RFIFOW(fd,34);
		short day = RFIFOW(fd,36);
		short hour = RFIFOW(fd,38);
		short minute = RFIFOW(fd,40);
		short second = RFIFOW(fd,42);
		RFIFOSKIP(fd,44);

		Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `account_id`,`name` FROM `%s` WHERE `name` = '%s'", schema_config.char_db, esc_name) )
			Sql_ShowDebug(sql_handle);
		else
		if( Sql_NumRows(sql_handle) == 0 )
		{
			result = 1; // 1-player not found
		}
		else
		if( SQL_SUCCESS != Sql_NextRow(sql_handle) )
			Sql_ShowDebug(sql_handle);
			//FIXME: set proper result value?
		else
		{
			char name[NAME_LENGTH];
			int account_id;
			char* data;

			Sql_GetData(sql_handle, 0, &data, NULL); account_id = atoi(data);
			Sql_GetData(sql_handle, 1, &data, NULL); safestrncpy(name, data, sizeof(name));

			if( login_fd <= 0 )
				result = 3; // 3-login-server offline
			//FIXME: need to move this check to login server [ultramage]
//				else
//				if( acc != -1 && isGM(acc) < isGM(account_id) )
//					result = 2; // 2-gm level too low
			else
			switch( type ) {
			case 1: // block
					WFIFOHEAD(login_fd,10);
					WFIFOW(login_fd,0) = 0x2724;
					WFIFOL(login_fd,2) = account_id;
					WFIFOL(login_fd,6) = 5; // new account status
					WFIFOSET(login_fd,10);
			break;
			case 2: // ban
					WFIFOHEAD(login_fd,18);
					WFIFOW(login_fd, 0) = 0x2725;
					WFIFOL(login_fd, 2) = account_id;
					WFIFOW(login_fd, 6) = year;
					WFIFOW(login_fd, 8) = month;
					WFIFOW(login_fd,10) = day;
					WFIFOW(login_fd,12) = hour;
					WFIFOW(login_fd,14) = minute;
					WFIFOW(login_fd,16) = second;
					WFIFOSET(login_fd,18);
			break;
			case 3: // unblock
					WFIFOHEAD(login_fd,10);
					WFIFOW(login_fd,0) = 0x2724;
					WFIFOL(login_fd,2) = account_id;
					WFIFOL(login_fd,6) = 0; // new account status
					WFIFOSET(login_fd,10);
			break;
			case 4: // unban
					WFIFOHEAD(login_fd,6);
					WFIFOW(login_fd,0) = 0x272a;
					WFIFOL(login_fd,2) = account_id;
					WFIFOSET(login_fd,6);
			break;
			case 5: // changesex
					WFIFOHEAD(login_fd,6);
					WFIFOW(login_fd,0) = 0x2727;
					WFIFOL(login_fd,2) = account_id;
					WFIFOSET(login_fd,6);
			break;
			}
		}

		Sql_FreeResult(sql_handle);

		// send answer if a player ask, not if the server ask
		if( acc != -1 && type != 5) { // Don't send answer for changesex
			WFIFOHEAD(fd,34);
			WFIFOW(fd, 0) = 0x2b0f;
			WFIFOL(fd, 2) = acc;
			safestrncpy((char*)WFIFOP(fd,6), name, NAME_LENGTH);
			WFIFOW(fd,30) = type;
			WFIFOW(fd,32) = result;
			WFIFOSET(fd,34);
		}
	}
	return 1;
}

int char_parsemap_updfamelist(int fd, int id){
	if (RFIFOREST(fd) < 11)
		return 0;
	{
		int cid = RFIFOL(fd, 2);
		int fame = RFIFOL(fd, 6);
		char type = RFIFOB(fd, 10);
		int size;
		struct fame_list* list;
		int player_pos;
		int fame_pos;

		switch(type)
		{
			case 1:  size = fame_list_size_smith;   list = smith_fame_list;   break;
			case 2:  size = fame_list_size_chemist; list = chemist_fame_list; break;
			case 3:  size = fame_list_size_taekwon; list = taekwon_fame_list; break;
			default: size = 0;                      list = NULL;              break;
		}

		ARR_FIND(0, size, player_pos, list[player_pos].id == cid);// position of the player
		ARR_FIND(0, size, fame_pos, list[fame_pos].fame <= fame);// where the player should be

		if( player_pos == size && fame_pos == size )
			;// not on list and not enough fame to get on it
		else if( fame_pos == player_pos )
		{// same position
			list[player_pos].fame = fame;
			char_update_fame_list(type, player_pos, fame);
		}
		else
		{// move in the list
			if( player_pos == size )
			{// new ranker - not in the list
				ARR_MOVE(size - 1, fame_pos, list, struct fame_list);
				list[fame_pos].id = cid;
				list[fame_pos].fame = fame;
				char_loadName(cid, list[fame_pos].name);
			}
			else
			{// already in the list
				if( fame_pos == size )
					--fame_pos;// move to the end of the list
				ARR_MOVE(player_pos, fame_pos, list, struct fame_list);
				list[fame_pos].fame = fame;
			}
			char_send_fame_list(-1);
		}

		RFIFOSKIP(fd,11);
	}
	return 1;
}

void char_send_ackdivorce(int partner_id1, int partner_id2){
	unsigned char buf[64]; //buf 10 ?
	WBUFW(buf,0) = 0x2b12;
	WBUFL(buf,2) = partner_id1;
	WBUFL(buf,6) = partner_id2;
	mapif_sendall(buf,10);
}

int char_parsemap_reqdivorce(int fd, int id){
	if( RFIFOREST(fd) < 10 )
		return 0;
	divorce_char_sql(RFIFOL(fd,2), RFIFOL(fd,6));
	RFIFOSKIP(fd,10);
	return 1;
}
int char_parsemap_updmapinfo(int fd, int id){
	if( RFIFOREST(fd) < 14 )
		return 0;
	{
		char esc_server_name[sizeof(charserv_config.server_name)*2+1];
		Sql_EscapeString(sql_handle, esc_server_name, charserv_config.server_name);
		if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` SET `index`='%d',`name`='%s',`exp`='%d',`jexp`='%d',`drop`='%d'",
			schema_config.ragsrvinfo_db, fd, esc_server_name, RFIFOL(fd,2), RFIFOL(fd,6), RFIFOL(fd,10)) )
			Sql_ShowDebug(sql_handle);
		RFIFOSKIP(fd,14);
	}
	return 1;
}
int char_parsemap_setcharoffline(int fd, int id){
	if (RFIFOREST(fd) < 6)
		return 0;
	set_char_offline(RFIFOL(fd,2),RFIFOL(fd,6));
	RFIFOSKIP(fd,10);
	return 1;
}

int char_parsemap_setalloffline(int fd, int id){
	set_all_offline(id);
	RFIFOSKIP(fd,2);
	return 1;
}

int char_parsemap_setcharonline(int fd, int id){
	if (RFIFOREST(fd) < 10)
		return 0;
	set_char_online(id, RFIFOL(fd,2),RFIFOL(fd,6));
	RFIFOSKIP(fd,10);
	return 1;
}

int char_parsemap_reqfamelist(int fd, int id){
	if (RFIFOREST(fd) < 2)
		return 0;
	char_read_fame_list();
	char_send_fame_list(-1);
	RFIFOSKIP(fd,2);
	return 1;
}

int char_parsemap_save_scdata(int fd, int id){
	if (RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2))
		return 0;
	{
#ifdef ENABLE_SC_SAVING
		int count, aid, cid;

		aid = RFIFOL(fd, 4);
		cid = RFIFOL(fd, 8);
		count = RFIFOW(fd, 12);

		if( count > 0 )
		{
			struct status_change_data data;
			StringBuf buf;
			int i;

			StringBuf_Init(&buf);
			StringBuf_Printf(&buf, "INSERT INTO `%s` (`account_id`, `char_id`, `type`, `tick`, `val1`, `val2`, `val3`, `val4`) VALUES ", schema_config.scdata_db);
			for( i = 0; i < count; ++i )
			{
				memcpy (&data, RFIFOP(fd, 14+i*sizeof(struct status_change_data)), sizeof(struct status_change_data));
				if( i > 0 )
					StringBuf_AppendStr(&buf, ", ");
				StringBuf_Printf(&buf, "('%d','%d','%hu','%d','%d','%d','%d','%d')", aid, cid,
					data.type, data.tick, data.val1, data.val2, data.val3, data.val4);
			}
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
				Sql_ShowDebug(sql_handle);
			StringBuf_Destroy(&buf);
		}
#endif
		RFIFOSKIP(fd, RFIFOW(fd, 2));
	}
	return 1;
}

int char_parsemap_keepalive(int fd, int id){
	WFIFOHEAD(fd,2);
	WFIFOW(fd,0) = 0x2b24;
	WFIFOSET(fd,2);
	RFIFOSKIP(fd,2);
	return 1;
}

int char_parsemap_reqauth(int fd, int id){
	if (RFIFOREST(fd) < 19)
		return 0;

	{
		int account_id;
		int char_id;
		int login_id1;
		char sex;
		uint32 ip;
		struct auth_node* node;
		struct mmo_charstatus* cd;
		struct mmo_charstatus char_dat;
		DBMap*  auth_db = char_get_authdb();
		DBMap* char_db_ = char_get_chardb();

		account_id = RFIFOL(fd,2);
		char_id    = RFIFOL(fd,6);
		login_id1  = RFIFOL(fd,10);
		sex        = RFIFOB(fd,14);
		ip         = ntohl(RFIFOL(fd,15));
		RFIFOSKIP(fd,19);

		node = (struct auth_node*)idb_get(auth_db, account_id);
		cd = (struct mmo_charstatus*)uidb_get(char_db_,char_id);
		if( cd == NULL )
		{	//Really shouldn't happen.
			mmo_char_fromsql(char_id, &char_dat, true);
			cd = (struct mmo_charstatus*)uidb_get(char_db_,char_id);
		}
		if( runflag == CHARSERVER_ST_RUNNING &&
			cd != NULL &&
			node != NULL &&
			node->account_id == account_id &&
			node->char_id == char_id &&
			node->login_id1 == login_id1 &&
			node->sex == sex /*&&
			node->ip == ip*/ )
		{// auth ok
			cd->sex = sex;

			WFIFOHEAD(fd,25 + sizeof(struct mmo_charstatus));
			WFIFOW(fd,0) = 0x2afd;
			WFIFOW(fd,2) = 25 + sizeof(struct mmo_charstatus);
			WFIFOL(fd,4) = account_id;
			WFIFOL(fd,8) = node->login_id1;
			WFIFOL(fd,12) = node->login_id2;
			WFIFOL(fd,16) = (uint32)node->expiration_time; // FIXME: will wrap to negative after "19-Jan-2038, 03:14:07 AM GMT"
			WFIFOL(fd,20) = node->group_id;
			WFIFOB(fd,24) = node->changing_mapservers;
			memcpy(WFIFOP(fd,25), cd, sizeof(struct mmo_charstatus));
			WFIFOSET(fd, WFIFOW(fd,2));

			// only use the auth once and mark user online
			idb_remove(auth_db, account_id);
			set_char_online(id, char_id, account_id);
		}
		else
		{// auth failed
			WFIFOHEAD(fd,19);
			WFIFOW(fd,0) = 0x2b27;
			WFIFOL(fd,2) = account_id;
			WFIFOL(fd,6) = char_id;
			WFIFOL(fd,10) = login_id1;
			WFIFOB(fd,14) = sex;
			WFIFOL(fd,15) = htonl(ip);
			WFIFOSET(fd,19);
		}
	}
	return 1;
}

int char_parsemap_updmapip(int fd, int id){
	if (RFIFOREST(fd) < 6) return 0;
	server[id].ip = ntohl(RFIFOL(fd, 2));
	ShowInfo("Updated IP address of map-server #%d to %d.%d.%d.%d.\n", id, CONVIP(server[id].ip));
	RFIFOSKIP(fd,6);
	return 1;
}

int char_parsemap_fw_configstats(int fd, int id){
	if( RFIFOREST(fd) < RFIFOW(fd,4) )
		return 0;/* packet wasn't fully received yet (still fragmented) */
	else {
		int sfd;/* stat server fd */
		RFIFOSKIP(fd, 2);/* we skip first 2 bytes which are the 0x3008, so we end up with a buffer equal to the one we send */

		if( (sfd = make_connection(host2ip("stats.rathena.org"),(uint16)25421,true,10) ) == -1 ) {
			RFIFOSKIP(fd, RFIFOW(fd,2) );/* skip this packet */
			return 0;/* connection not possible, we drop the report */
		}

		session[sfd]->flag.server = 1;/* to ensure we won't drop our own packet */
		WFIFOHEAD(sfd, RFIFOW(fd,2) );
		memcpy((char*)WFIFOP(sfd,0), (char*)RFIFOP(fd, 0), RFIFOW(fd,2));
		WFIFOSET(sfd, RFIFOW(fd,2) );
		flush_fifo(sfd);
		do_close(sfd);
		RFIFOSKIP(fd, RFIFOW(fd,2) );/* skip this packet */
	}
	return 1;
}

int parse_frommap(int fd){
	int id; //mapserv id
	ARR_FIND( 0, ARRAYLENGTH(server), id, server[id].fd == fd );
	if( id == ARRAYLENGTH(server) )
	{// not a map server
		ShowDebug("parse_frommap: Disconnecting invalid session #%d (is not a map-server)\n", fd);
		do_close(fd);
		return 0;
	}
	if( session[fd]->flag.eof )
	{
		do_close(fd);
		server[id].fd = -1;
		mapif_on_disconnect(id);
		return 0;
	}

	while(RFIFOREST(fd) >= 2){
		switch(RFIFOW(fd,0)){
		// Receiving map names list from the map-server
		case 0x2afa: char_parsemap_getmapname(fd,id); break;
		//Packet command is now used for sc_data request. [Skotlex]
		case 0x2afc: char_parsemap_askscdata(fd,id); break;
		//MAP user count
		case 0x2afe: char_parsemap_getusercount(fd,id); break; //get nb user
		case 0x2aff: char_parsemap_regmapuser(fd,id); break; //register users
		// Receive character data from map-server for saving
		case 0x2b01: char_parsemap_reqsavechar(fd,id); break;
		// req char selection;
		case 0x2b02: char_parsemap_authok(fd,id); break;
		// request "change map server"
		case 0x2b05: char_parsemap_reqchangemapserv(fd,id); break;
		// Remove RFIFOL(fd,6) (friend_id) from RFIFOL(fd,2) (char_id) friend list [Ind]
		case 0x2b07: char_parsemap_askrmfriend(fd,id); break;
		// char name request
		case 0x2b08: char_parsemap_reqcharname(fd,id); break;
		// Map server send information to change an email of an account -> login-server
		case 0x2b0c: char_parsemap_reqnewemail(fd,id); break;
		// Request from map-server to change an account's status (will just be forwarded to login server)
		case 0x2b0e: char_parsemap_fwlog_changestatus(fd,id); break;
		// Update and send fame ranking list
		case 0x2b10: char_parsemap_updfamelist(fd,id); break;
		// Divorce chars
		case 0x2b11: char_parsemap_reqdivorce(fd,id); break;
		// Receive rates [Wizputer]
		case 0x2b16: char_parsemap_updmapinfo(fd,id); break;
		// Character disconnected set online 0 [Wizputer]
		case 0x2b17: char_parsemap_setcharoffline(fd,id); break;
		// Reset all chars to offline [Wizputer]
		case 0x2b18: char_parsemap_setalloffline(fd,id); break;
		// Character set online [Wizputer]
		case 0x2b19: char_parsemap_setcharonline(fd,id); break;
		// Build and send fame ranking lists [DracoRPG]
		case 0x2b1a: char_parsemap_reqfamelist(fd,id); break;
		//Request to save status change data. [Skotlex]
		case 0x2b1c: char_parsemap_save_scdata(fd,id); break;
		// map-server alive packet
		case 0x2b23: char_parsemap_keepalive(fd,id); break;
		// auth request from map-server
		case 0x2b26: char_parsemap_reqauth(fd,id); break;
		// ip address update
		case 0x2736: char_parsemap_updmapip(fd,id); break;
		// transmit emu usage for anom stats
		case 0x3008: char_parsemap_fw_configstats(fd,id); break;
		default:
		{
			// inter server - packet
			int r = inter_parse_frommap(fd);
			if (r == 1) break;		// processed
			if (r == 2) return 0;	// need more packet
			// no inter server packet. no char server packet -> disconnect
			ShowError("Unknown packet 0x%04x from map server, disconnecting.\n", RFIFOW(fd,0));
			set_eof(fd);
			return 0;
		}
		} // switch
	} // while

	return 0;
}


// Initialization process (currently only initialization inter_mapif)
int char_mapif_init(int fd){
	return inter_mapif_init(fd);
}
/// Initializes a server structure.
void mapif_server_init(int id)
{
	memset(&server[id], 0, sizeof(server[id]));
	server[id].fd = -1;
}
/// Destroys a server structure.
void mapif_server_destroy(int id){
	if( server[id].fd == -1 )
	{
		do_close(server[id].fd);
		server[id].fd = -1;
	}
}

void do_init_mapif(void)
{
	int i;
	for( i = 0; i < ARRAYLENGTH(server); ++i )
		mapif_server_init(i);
}

/// Resets all the data related to a server.
void mapif_server_reset(int id){
	int i,j;
	unsigned char buf[16384];
	int fd = server[id].fd;
	DBMap* online_char_db = char_get_onlinedb();

	//Notify other map servers that this one is gone. [Skotlex]
	WBUFW(buf,0) = 0x2b20;
	WBUFL(buf,4) = htonl(server[id].ip);
	WBUFW(buf,8) = htons(server[id].port);
	j = 0;
	for(i = 0; i < MAX_MAP_PER_SERVER; i++)
		if (server[id].map[i])
			WBUFW(buf,10+(j++)*4) = server[id].map[i];
	if (j > 0) {
		WBUFW(buf,2) = j * 4 + 10;
		mapif_sendallwos(fd, buf, WBUFW(buf,2));
	}
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `index`='%d'", schema_config.ragsrvinfo_db, server[id].fd) )
		Sql_ShowDebug(sql_handle);
	online_char_db->foreach(online_char_db,char_db_setoffline,id); //Tag relevant chars as 'in disconnected' server.
	mapif_server_destroy(id);
	mapif_server_init(id);
}


/// Called when the connection to a Map Server is disconnected.
void mapif_on_disconnect(int id){
	ShowStatus("Map-server #%d has disconnected.\n", id);
	mapif_server_reset(id);
}



void do_final_mapif(void)
{
	int i;
	for( i = 0; i < ARRAYLENGTH(server); ++i )
		mapif_server_destroy(i);
}