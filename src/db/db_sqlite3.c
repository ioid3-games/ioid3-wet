/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2016 ET:Legacy team <mail@etlegacy.com>
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
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
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
 * @file db_sqlite3.c
 * @brief ET: Legacy SQL interface
 *
 * TODO: - extend our db scheme
 * - implement version system & auto updates
 * - ...
 * - clean up
 *
 *        Tutorial: http://zetcode.com/db/sqlitec/
 */

#include "db_sql.h"

cvar_t *db_mode;
cvar_t *db_url;

sqlite3  *db;
qboolean isDBActive;

int DB_Init() {
	char *to_ospath;

	isDBActive = qfalse;

	db_mode = Cvar_Get("db_mode", "0", CVAR_ARCHIVE | CVAR_LATCH);
	db_url = Cvar_Get("db_url", "etl.db", CVAR_ARCHIVE | CVAR_LATCH); // filename in path (not real DB URL for now)

	if (db_mode->integer < 1 || db_mode->integer > 2) {
		Com_Printf("... DBMS is disabled\n");
		return 0; // return 0! - see isDBActive
	}

	Com_Printf("SQLite3 libversion %s - database URL '%s' - %s\n", sqlite3_libversion(), db_url->string, db_mode->integer == 1 ? "in-memory" : "in file");

	if (!db_url->string[0]) // FIXME: check extension db
	{
		Com_Printf("... can't init database - empty URL\n");
		return 1;
	}

	to_ospath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), db_url->string, "");
	to_ospath[strlen(to_ospath) - 1] = '\0';

	if (FS_SV_FileExists(db_url->string)) {
		int result;

		Com_Printf("... loading existing database '%s'\n", to_ospath);

		if (db_mode->integer == 1) {
			// init memory table
			//result = sqlite3_open(":memory:", &db); // memory table, not shared see https://www.sqlite.org/inmemorydb.html
			result = sqlite3_open("file::memory:?cache=shared", &db); // In-memory databases with shared cache

			if (result != SQLITE_OK) {
				Com_Printf("... failed to open memory database - error: %s\n", sqlite3_errstr(result));
				(void) sqlite3_close(db);
				return 1;
			}
			// load from disk into memory
			result = DB_LoadOrSaveDb(db, to_ospath, 0);

			if (result != SQLITE_OK) {
				Com_Printf("... WARNING can't load database file %s\n", db_url->string);
				return 1;
			}
		} else if (db_mode->integer == 2) {
			result = sqlite3_open(to_ospath, &db);

			if (result != SQLITE_OK) {
				Com_Printf("... failed to open file database - error: %s\n", sqlite3_errstr(result));
				(void) sqlite3_close(db);
				return 1;
			}
		} else {
			Com_Printf("... failed to open database - unknown mode\n");
			return 1;
		}

		Com_Printf("... database file '%s' loaded\n", to_ospath);
	} else // create new
	{
		int result;

		Com_Printf("... no database file '%s' found ... creating now\n", to_ospath);
		result = DB_Create();

		if (result != 0) {
			Com_Printf("... WARNING can't create database [%i]\n", result);
			return 1;
		}
		// save memory db to disk
		if (db_mode->integer == 1) {
			result = DB_SaveMemDB();

			if (result != SQLITE_OK) {
				Com_Printf("... WARNING can't save memory database file [%i]\n", result);
				return 1;
			}
		}
	}

	Com_Printf("SQLite3 ET: L [%i] database '%s' init - autocommit %i\n", ETL_DBMS_VERSION, to_ospath, sqlite3_get_autocommit(db));

	isDBActive = qtrue;
	return 0;
}

/**
 * @brief creates tables and populates our scheme
 */
static int DB_Create_Schema() {
	int result;
	char *err_msg = 0;

	// FIXME:
	// - split this into client and server DB?!
	// - set index for search fields // CREATE INDEX player_guid ON PLAYER(guid)

	// version table
	char *sql = "DROP TABLE IF EXISTS etl_version;"
	            "CREATE TABLE etl_version (Id INT PRIMARY KEY NOT NULL, name TEXT, sql TEXT, created TEXT);"
	            "INSERT INTO etl_version VALUES (1, 'ET: L DBMS', '', CURRENT_TIMESTAMP);"; // FIXME: separate version inserts for updates ...

	result = sqlite3_exec(db, sql, 0, 0, &err_msg);

	if (result != SQLITE_OK) {
		Com_Printf("SQLite3 failed to create table version: %s\n", err_msg);
		sqlite3_free(err_msg);
		return 1;
	}
	// ban/mute table (ensure we can also do IP range ban entries)
	// type = mute/ban
	// af = AddressFamily
	sql = "DROP TABLE IF EXISTS ban;"
	      "CREATE TABLE ban (Id INT PRIMARY KEY NOT NULL, address TEXT, guid TEXT, type INT NOT NULL, reason TEXT, af INT, length TEXT, expires TEXT, created TEXT, updated TEXT);"
	      "CREATE INDEX ban_address_idx ON ban(address);"
	      "CREATE INDEX ban_guid_idx ON ban(guid);";
	// expires?

	result = sqlite3_exec(db, sql, 0, 0, &err_msg);

	if (result != SQLITE_OK) {
		Com_Printf("SQLite3 failed to create table ban: %s\n", err_msg);
		sqlite3_free(err_msg);
		return 1;
	}
	// player/client table
	// FIXME: do we want to track player names as PB did?
	sql = "DROP TABLE IF EXISTS player;"
	      "CREATE TABLE player (Id INT PRIMARY KEY NOT NULL, name TEXT, guid TEXT, user TEXT, password TEXT, mail TEXT, bans INT, mutes INT, created TEXT, updated TEXT);"
	      "CREATE INDEX player_name_idx ON player(name);"
	      "CREATE INDEX player_guid_idx ON player(guid);";

	result = sqlite3_exec(db, sql, 0, 0, &err_msg);

	if (result != SQLITE_OK) {
		Com_Printf("... failed to create table player: %s\n", err_msg);
		sqlite3_free(err_msg);
		return 1;
	}
	// session table - server side tracking of players, client side tracking of games
	// note: we might drop length field (created = start, updated = end of session
	sql = "DROP TABLE IF EXISTS session;"
	      "CREATE TABLE session (Id INT PRIMARY KEY NOT NULL, pId INT , address TEXT, port INT, type INT, duration TEXT, map TEXT, length TEXT, created TEXT, updated TEXT, FOREIGN KEY(pId) REFERENCES player(Id));"
	      "";

	result = sqlite3_exec(db, sql, 0, 0, &err_msg);

	if (result != SQLITE_OK) {
		Com_Printf("... failed to create table session: %s\n", err_msg);
		sqlite3_free(err_msg);
		return 1;
	}
	// FIXME:
	// add server table - store available maps, uptime & such
	// add favourite table ?! (client)
	return 0;
}

int DB_Create() {
	int result;

	if (db_mode->integer == 1) {
		//result = sqlite3_open(":memory:", &db); // memory table, not shared see https://www.sqlite.org/inmemorydb.html
		result = sqlite3_open("file::memory:?cache=shared", &db); // In-memory databases with shared cache

		if (result != SQLITE_OK) {
			Com_Printf("... failed to create memory database - error: %s\n", sqlite3_errstr(result));
			(void) sqlite3_close(db);
			return 1;
		}
	} else if (db_mode->integer == 2) {
		char *to_ospath;

		to_ospath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), db_url->string, ""); // FIXME: check for empty db_url
		to_ospath[strlen(to_ospath) - 1] = '\0';

		result = sqlite3_open(to_ospath, &db);

		if (result != SQLITE_OK) {
			Com_Printf("... failed to create file database - error: %s\n", sqlite3_errstr(result));
			(void) sqlite3_close(db);
			return 1;
		}
	} else {
		Com_Printf("... DB_Create failed - unknown mode\n");
		return 1;
	}

	result = DB_Create_Schema();

	if (result != 0) {
		Com_Printf("... failed to create database schema\n");
		(void) sqlite3_close(db);
		return 1;
	}

	Com_Printf("... database %s created\n", db_url->string);
	return 0;
}

/**
 * @brief saves memory db to disk
 */
int DB_SaveMemDB() {
	if (db_mode->integer == 1) {
		int result, msec;
		char *to_ospath;

		// FIXME: check for empty db_url

		to_ospath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), db_url->string, "");
		to_ospath[strlen(to_ospath) - 1] = '\0';

		msec = Sys_Milliseconds();

		result = DB_LoadOrSaveDb(db, to_ospath, 1);

		if (result != SQLITE_OK) {
			Com_Printf("... WARNING can't save memory database file [%i]\n", result);
			return 1;
		}

		Com_Printf("SQLite3 in-memory tables saved to disk @[%s] in [%i] msec\n", to_ospath, (Sys_Milliseconds() - msec));
	} else {
		Com_Printf("saveMemDB called for unknown database mode\n");
	}

	return 0;
}

int DB_Close() {
	int result;

	if (!isDBActive) {
		Com_Printf("SQLite3 can't close database - not active.\n");
		return 1;
	}
	// save memory db to disk
	if (db_mode->integer == 1) {
		result = DB_SaveMemDB();

		if (result != SQLITE_OK) {
			Com_Printf("... WARNING can't save memory database file [%i]\n", result);
			// let's close the db ...
		}
	}

	result = sqlite3_close(db);
	isDBActive = qfalse;

	if (result != SQLITE_OK) {
		Com_Printf("SQLite3 failed to close database.\n");
		return 1;
	}

	Com_Printf("SQLite3 database closed.\n");
	return 0;
}

/**
 * @brief Perform an online backup of our database db to the database file named
 * by zFilename. Used to store our memory table in between map changes to disk
 *
 * This function copies 5 database pages from pDb to
 * zFilename, then unlocks pDb and sleeps for 250 ms, then repeats the
 * process until the entire database is backed up.
 *
 * The third argument passed to this function must be a pointer to a progress
 * function. After each set of 5 pages is backed up, the progress function
 * is invoked with two integer parameters: the number of pages left to
 * copy, and the total number of pages in the source file. This information
 * may be used, for example, to update a GUI progress bar.
 *
 * While this function is running, another thread may use the database pDb, or
 * another process may access the underlying database file via a separate
 * connection.
 *
 * If the backup process is successfully completed, SQLITE_OK is returned.
 * Otherwise, if an error occurs, an SQLite error code is returned.
 */
int DB_BackupDB(const char *zFilename, void (*xProgress)(int, int)) // Progress function to invoke
{
	int rc; // Function return code
	sqlite3        *pFile; // Database connection opened on zFilename
	sqlite3_backup *pBackup; // Backup handle used to copy data

	// Open the database file identified by zFilename.
	rc = sqlite3_open(zFilename, &pFile);

	if (rc == SQLITE_OK) {
		// Open the sqlite3_backup object used to accomplish the transfer
		pBackup = sqlite3_backup_init(pFile, "main", db, "main");

		if (pBackup) {
			// Each iteration of this loop copies 5 database pages from database
			// pDb to the backup database. If the return value of backup_step()
			// indicates that there are still further pages to copy, sleep for
			// 250 ms before repeating.
			do
			{
				rc = sqlite3_backup_step(pBackup, 5);
				xProgress(sqlite3_backup_remaining(pBackup), sqlite3_backup_pagecount(pBackup));

				if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
					sqlite3_sleep(250);
				}
			}

			while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

			// Release resources allocated by backup_init().
			(void) sqlite3_backup_finish(pBackup);
		}

		rc = sqlite3_errcode(pFile);
	}
	// Close the database connection opened on database file zFilename and return the result of this function.
	(void) sqlite3_close(pFile);
	return rc;
}

/**
 * @brief This function is used to load the contents of a database file on disk
 * into the "main" database of open database connection pInMemory, or
 * to save the current contents of the database opened by pInMemory into
 * a database file on disk. pInMemory is probably an in-memory database,
 * but this function will also work fine if it is not.
 *
 * Parameter zFilename points to a nul-terminated string containing the
 * name of the database file on disk to load from or save to. If parameter
 * isSave is non-zero, then the contents of the file zFilename are
 * overwritten with the contents of the database opened by pInMemory. If
 * parameter isSave is zero, then the contents of the database opened by
 * pInMemory are replaced by data loaded from the file zFilename.
 *
 * If the operation is successful, SQLITE_OK is returned. Otherwise, if
 * an error occurs, an SQLite error code is returned.
 */
int DB_LoadOrSaveDb(sqlite3 *pInMemory, const char *zFilename, int isSave) {
	int rc; // Function return code
	sqlite3        *pFile; // Database connection opened on zFilename
	sqlite3_backup *pBackup; // Backup object used to copy data
	sqlite3        *pTo; // Database to copy to (pFile or pInMemory)
	sqlite3        *pFrom; // Database to copy from (pFile or pInMemory)

	// Open the database file identified by zFilename. Exit early if this fails for any reason.
	rc = sqlite3_open(zFilename, &pFile);

	if (rc == SQLITE_OK) {
		// If this is a 'load' operation (isSave==0), then data is copied
		// from the database file just opened to database pInMemory.
		// Otherwise, if this is a 'save' operation (isSave==1), then data
		// is copied from pInMemory to pFile.  Set the variables pFrom and
		// pTo accordingly.
		pFrom = (isSave ? pInMemory : pFile);
		pTo = (isSave ? pFile     : pInMemory);

		// Set up the backup procedure to copy from the "main" database of
		// connection pFile to the main database of connection pInMemory.
		// If something goes wrong, pBackup will be set to NULL and an error
		// code and  message left in connection pTo.
		//
		// If the backup object is successfully created, call backup_step()
		// to copy data from pFile to pInMemory. Then call backup_finish()
		// to release resources associated with the pBackup object.  If an
		// error occurred, then  an error code and message will be left in
		// connection pTo. If no error occurred, then the error code belonging
		// to pTo is set to SQLITE_OK.
		pBackup = sqlite3_backup_init(pTo, "main", pFrom, "main");

		if (pBackup) {
			(void) sqlite3_backup_step(pBackup, -1);
			(void) sqlite3_backup_finish(pBackup);
		}

		rc = sqlite3_errcode(pTo);
	}
	// Close the database connection opened on database file zFilename and return the result of this function.
	(void) sqlite3_close(pFile);
	return rc;
}

int DB_callback(void *NotUsed, int argc, char **argv, char **azColName) {
	int i;
	// NotUsed = 0;

	//for (i = 0; i < argc; i++)
	//{
	//	Com_Printf("%s ", azColName[i]);
	//}
	//Com_Printf("\n");

	Com_Printf("|");

	for (i = 0; i < argc; i++) {
		Com_Printf("%s|", argv[i] && argv[i][0] ? argv[i] : "NULL");
	}

	Com_Printf("\n");

	return 0;
}


/**
 * @brief Get the last inserted ROWID
 * see also "SELECT last_insert_rowid()"
 */
int DB_last_insert_rowid() {
	if (db) {
		return sqlite3_last_insert_rowid(db);
	}

	return -1;
}
