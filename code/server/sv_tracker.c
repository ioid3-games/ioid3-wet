/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright(C) 1999 - 2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright(C) 2012 - 2018 ET:Legacy team < mail@etlegacy.com > 
 * Copyright(C) 2012 Konrad Mosoń < mosonkonrad@gmail.com > 
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see < http://www.gnu.org/licenses/ > .
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**

/*
======================================================================================================================================

	Sends game statistics to Tracker

=======================================================================================================================================
*/
#ifdef FEATURE_TRACKER
#include "sv_tracker.h"

long t;
int waittime = 15; // seconds
char expect[16];
int expectnum;
qboolean maprunning = qfalse;
int querycl = -1;

enum {
	TR_BOT_NONE,
	TR_BOT_CONNECT
} catchBot;
qboolean catchBotNum = 0;

netadr_t addr;
#ifdef TRACKER_DEBUG
netadr_t local;
#endif
char infostring[MAX_INFO_STRING];
char *Tracker_getGUID(client_t *cl);

/*
=======================================================================================================================================
Tracker_Send

Sends data to Tracker. Formatted data to send.
=======================================================================================================================================
*/
void Tracker_Send(char *format, ...) {
	va_list argptr;
	char msg[MAX_MSGLEN];

	va_start(argptr, format);
	Q_vsnprintf(msg, sizeof(msg), format, argptr);
	va_end(argptr);

	NET_OutOfBandPrint(NS_SERVER, addr, "%s", msg);
#ifdef TRACKER_DEBUG
	NET_OutOfBandPrint(NS_SERVER, local, "%s", msg);
#endif
}

/*
=======================================================================================================================================
Tracker_Init

Initialize Tracker support.
=======================================================================================================================================
*/
void Tracker_Init(void) {
	char *tracker;

	if (!(sv_advert->integer & SVA_TRACKER)) {
		Com_Printf("Tracker: Server communication disabled by sv_advert.\n");
		return;
	}

	tracker = Cvar_VariableString("sv_tracker");
	t = time(0);
	expectnum = 0;

	NET_StringToAdr(tracker, &addr, NA_IP);
#ifdef TRACKER_DEBUG
	NET_StringToAdr("127.0.0.1:6066", &local, NA_IP);
#endif
	Com_Printf("Tracker: Server communication enabled.\n");
}

/*
=======================================================================================================================================
Tracker_ServerStart

Send info about server startup.
=======================================================================================================================================
*/
void Tracker_ServerStart(void) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("start");
}

/*
=======================================================================================================================================
Tracker_ServerStop

Send info about server shutdown.
=======================================================================================================================================
*/
void Tracker_ServerStop(void) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("stop");
}

/*
=======================================================================================================================================
Tracker_ClientConnect

Send info about new client connected.
=======================================================================================================================================
*/
void Tracker_ClientConnect(client_t *cl) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("connect %i %s %s", (int)(cl - svs.clients), Tracker_getGUID(cl), cl->name);
}

/*
=======================================================================================================================================
Tracker_ClientDisconnect

Send info when client disconnects.
=======================================================================================================================================
*/
void Tracker_ClientDisconnect(client_t *cl) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("disconnect %i", (int)(cl - svs.clients));
}

/*
=======================================================================================================================================
Tracker_ClientName

Send info when player changes his name.
=======================================================================================================================================
*/
void Tracker_ClientName(client_t *cl) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	if (!*cl->name) {
		return;
	}

	Tracker_Send("name %i %s %s", (int)(cl - svs.clients), Tracker_getGUID(cl), Info_ValueForKey(cl->userinfo, "name"));
}

/*
=======================================================================================================================================
Tracker_Map

Send info when map has changed.
=======================================================================================================================================
*/
void Tracker_Map(char *mapname) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("map %s", mapname);
	maprunning = qtrue;
}

/*
=======================================================================================================================================
Tracker_MapRestart

Send info when map restarts. Allows counting time from 0 again on TB.
=======================================================================================================================================
*/
void Tracker_MapRestart(void) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("maprestart");
	maprunning = qtrue;
}

/*
=======================================================================================================================================
Tracker_MapEnd

Send info when map has finished. Sometimes intermission is very long, so TB can show appropriate info to players.
=======================================================================================================================================
*/
void Tracker_MapEnd(void) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	Tracker_Send("mapend");
	Tracker_requestWeaponStats();
	maprunning = qfalse;
}
#if 0
/*
=======================================================================================================================================
Tracker_TeamSwitch

Send info when player changes his team.
=======================================================================================================================================
*/
void Tracker_TeamSwitch(client_t *cl) {
	Tracker_Send("team %i", (int)(cl - svs.clients));
}
#endif // 0
/*
=======================================================================================================================================
Tracker_createClientInfo

Creates client information for other functions. clientNum Client ID (from 0 to MAX_CLIENTS).
NOTE: Just for internal use.
=======================================================================================================================================
*/
char *Tracker_createClientInfo(int clientNum) {
	playerState_t *ps;

	ps = SV_GameClientNum(clientNum);
	return va("%i\\%i\\%c\\%i\\%s", svs.clients[clientNum].ping, ps->persistant[PERS_SCORE], Info_ValueForKey(Cvar_InfoString(CVAR_SERVERINFO|CVAR_SERVERINFO_NOUPDATE), "P")[clientNum], ps->stats[STAT_PLAYER_CLASS], svs.clients[clientNum].name);
}

/*
=======================================================================================================================================
Tracker_requestWeaponStats

Request weapon stats data from mod.
=======================================================================================================================================
*/
void Tracker_requestWeaponStats(void) {
	int i;
	qboolean onlybots = qtrue;
	char *P;

	if (!maprunning) {
		return;
	}

	strcpy(infostring, Cvar_InfoString(CVAR_SERVERINFO|CVAR_SERVERINFO_NOUPDATE));

	P = Info_ValueForKey(infostring, "P");

	strcpy(expect, "ws");

	for (i = 0; i < sv_maxclients->value; i++) {
		if (svs.clients[i].state == CS_ACTIVE) {
			if (svs.clients[i].netchan.remoteAddress.type != NA_BOT) {
				onlybots = qfalse;
				querycl = i;
			}

			expectnum++;
		}
	}

	if (expectnum > 0) {
		Tracker_Send("wsc %i", expectnum);

		for (i = 0; i < sv_maxclients->value; i++) {
			if (svs.clients[i].state == CS_ACTIVE) {
				// send basic data is client is spectator
				if (P[i] == '3' || (svs.clients[i].netchan.remoteAddress.type == NA_BOT && onlybots)) {
					Tracker_Send("ws %i 0 0 0\\%s", i, Tracker_createClientInfo(i));
				}
			}
		}

		if (querycl >= 0) {
			SV_ExecuteClientCommand(&svs.clients[querycl], "statsall", qtrue, qfalse);
		}
	}
}

/*
=======================================================================================================================================
Tracker_Frame

Frame function.
=======================================================================================================================================
*/
void Tracker_Frame(int msec) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	if (catchBot == TR_BOT_CONNECT) {
		Tracker_ClientConnect(&svs.clients[catchBotNum]);
		catchBot = TR_BOT_NONE;
	}

	if (!(time(0) - waittime > t)) {
		return;
	}

	Tracker_Send("p"); // send ping to tb to show that server is still alive

	expectnum = 0; // reset before next statsall

	Tracker_requestWeaponStats();

	t = time(0);
}

/*
=======================================================================================================================================
Tracker_catchServerCommand

Catches server command. clientNum Client ID (from 0 to MAX_CLIENTS). Message sends by backend.
=======================================================================================================================================
*/
qboolean Tracker_catchServerCommand(int clientNum, char *msg) {
	int slot;

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return qfalse;
	}

	if (clientNum != querycl) {
		return qfalse;
	}

	if (expectnum == 0) {
		return qfalse;
	}

	if (!(!strncmp(expect, msg, strlen(expect)))) {
		return qfalse;
	}

	if (msg[strlen(msg) - 1] == '\n') {
		msg[strlen(msg) - 1] = '\0';
	}

	if (!Q_strncmp("ws", msg, 2)) {
		expectnum--;

		if (expectnum == 0) {
			strcpy(expect, "");
			querycl = -1;
		}

		slot = 0;

		sscanf(msg, "ws %i", &slot);
		Tracker_Send("%s\\%s", msg, Tracker_createClientInfo(slot));

		return qtrue;
	}

	return qfalse;
}

/*
=======================================================================================================================================
Tracker_catchBotConnect

Catch bot connection.
=======================================================================================================================================
*/
void Tracker_catchBotConnect(int clientNum) {

	if (!(sv_advert->integer & SVA_TRACKER)) {
		return;
	}

	catchBot = TR_BOT_CONNECT;
	catchBotNum = clientNum;
}

/*
=======================================================================================================================================
Tracker_getGUID

We prefer to use original cl_guid, but some mods has their own guid values.
=======================================================================================================================================
*/
char *Tracker_getGUID(client_t *cl) {

	if (*Info_ValueForKey(cl->userinfo, "cl_guid")) {
		return Info_ValueForKey(cl->userinfo, "cl_guid");
	} else {
		return "unknown";
	}
}

#endif // FEATURE_TRACKER
