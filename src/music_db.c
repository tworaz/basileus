/*-
 * Copyright (c) 2013 Peter Tworek
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stddef.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <tag_c.h>
#include <sqlite3.h>

#include "logger.h"
#include "music_db.h"
#include "mongoose.h"
#include "configuration.h"
#include "basileus-music-db.h"

#define PREP(db, stmt, str) \
	if (SQLITE_OK != sqlite3_prepare_v2((db), (str), -1, (stmt), NULL)) break;
#define BIND_TXT(stmt, pos, txt) \
	if (SQLITE_OK != sqlite3_bind_text((stmt), (pos), (txt), -1, 0)) break;
#define BIND_INT(stmt, pos, val) \
	if (SQLITE_OK != sqlite3_bind_int((stmt), (pos), (val))) break;
#define STEP(stmt) \
	if (SQLITE_DONE != sqlite3_step((stmt))) break;
#define FINALIZE(stmt) \
	if (SQLITE_OK != sqlite3_finalize((stmt))) break; else (stmt) = NULL;

typedef struct {
	sqlite3	        *db;
	cfg_t	        *cfg;
	pthread_mutex_t	 scan_mutex;
	pthread_t        scan_thread;
	int              scan_in_progress : 1;
	int              scan_terminate : 1;
} _music_db_t;

static int
music_db_add_file(_music_db_t *mdb, const char *path)
{
	const TagLib_AudioProperties *props = NULL;
	sqlite3_stmt *stmt = NULL;
	TagLib_File *file = NULL;
	TagLib_Tag *tag = NULL;
	char hash[33];
	int ret = 0;

	if ((file = taglib_file_new(path)) == NULL || !taglib_file_is_valid(file)) {
		log_debug("Skipping unrecoginzed file type: %s", path);
		return 0;
	}

	if ((tag = taglib_file_tag(file)) == NULL) {
		log_warning("Could not read %s tags, skipping ...", path);
		goto finish;
	}

	if ((props = taglib_file_audioproperties(file)) == NULL) {
		log_warning("Could not read %s audio properties, skipping ...", path);
		goto finish;
	}

	log_trace("Adding file to database: %s", path);

	char *artist = taglib_tag_artist(tag);
	char *title = taglib_tag_title(tag);
	char *album = taglib_tag_album(tag);
	int track = taglib_tag_track(tag);
	int length = taglib_audioproperties_length(props);

	mg_md5(hash, path, NULL);

	int done = 0;
	do {
		PREP(mdb->db, &stmt, "INSERT INTO artists (name) VALUES (?);");
		BIND_TXT(stmt, 1, artist);
		STEP(stmt);
		FINALIZE(stmt);

		PREP(mdb->db, &stmt, "INSERT INTO albums (name, artist_id) VALUES (?,"
			"(SELECT id FROM artists WHERE name=?));");
		BIND_TXT(stmt, 1, album);
		BIND_TXT(stmt, 2, artist);
		STEP(stmt);
		FINALIZE(stmt);

		PREP(mdb->db, &stmt, "INSERT INTO songs (title, path, hash, track, length, artist_id, album_id)"
			"VALUES (:1, :2, :3, :4, :5,"
			"(SELECT id FROM artists WHERE name=:6),"
			"(SELECT id FROM albums WHERE artist_id=(SELECT id FROM artists WHERE name=:6) AND name=:7));");
		BIND_TXT(stmt, 1, title);
		BIND_TXT(stmt, 2, path);
		BIND_TXT(stmt, 3, hash);
		BIND_INT(stmt, 4, track);
		BIND_INT(stmt, 5, length);
		BIND_TXT(stmt, 6, artist);
		BIND_TXT(stmt, 7, album);
		STEP(stmt);
		FINALIZE(stmt);

		done = 1;
	} while (0);

	if (!done) {
		log_error("Adding %s to music database failed: %s", path, sqlite3_errmsg(mdb->db));
		ret = 1;
	}

finish:
	if (stmt) {
		sqlite3_finalize(stmt);
	}
	taglib_tag_free_strings();
	taglib_file_free(file);

	return ret;
}

static int
music_db_scan_directory(_music_db_t *mdb, const char *dir)
{
	struct dirent *entry = NULL, *dirent_buf = NULL;
	DIR *dirp = NULL;;
	struct stat st;
	char *full_path;
	int ret = 0, len = 0, name_max = 0;

	if (lstat(dir, &st) != 0) {
		log_warning("Failed to stat %s: %s", dir, strerror(errno));
		return 0;
	}

	if (!S_ISDIR(st.st_mode)) {
		log_warning("Failed to scan %s: Not a directory", dir);
		return 0;
	}

	name_max = pathconf(dir, _PC_NAME_MAX);
	if (name_max == -1) {
		name_max = _POSIX_NAME_MAX;
	}

	dirent_buf = malloc(offsetof(struct dirent, d_name) + name_max + 1);
	if (dirent_buf == NULL) {
		log_error("Failed to allocate dirent buffer!");
		return 1;
	}

	if ((dirp = opendir(dir)) == NULL) {
		log_warning("Failed to open %s: %s", dir, strerror(errno));
		return 1;
	}

	log_trace("Scanning directory: %s", dir);

	while (0 == readdir_r(dirp, dirent_buf, &entry) && entry != NULL) {
		if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
			continue;
		}

		len = strlen(dir) + strlen(entry->d_name) + 2;
		full_path = (char *) malloc(len);
		if (full_path == NULL) {
			log_error("Failed to allocate buffer for path!");
			ret = ENOMEM;
			break;
		}
		memset(full_path, 0, len);
		strncat(full_path, dir, strlen(dir));
		strncat(full_path, "/", 1);
		strncat(full_path, entry->d_name, strlen(entry->d_name));

		if (entry->d_type == DT_DIR) {
			if ((ret = music_db_scan_directory(mdb, full_path))) {
				break;
			}
		} else if (entry->d_type == DT_REG) {
			if ((ret = music_db_add_file(mdb, full_path))) {
				break;
			}
		}
		free(full_path);
		full_path = NULL;

		if ((ret = pthread_mutex_lock(&mdb->scan_mutex))) {
			log_error("Failed to lock scan mutex: %d!", ret);
			break;
		}
		if (mdb->scan_terminate) {
			pthread_mutex_unlock(&mdb->scan_mutex);
			ret = EINTR;
			break;
		}
		if ((ret = pthread_mutex_unlock(&mdb->scan_mutex))) {
			log_error("Failed to unlock scan mutex: %d!", ret);
			exit(ret);
		}
	}

	if (full_path != NULL) {
		free(full_path);
		full_path = NULL;
	}

	if (dirent_buf != NULL) {
		free(dirent_buf);
		dirent_buf = NULL;
	}

	closedir(dirp);

	return ret;
}

static void *
music_db_scan_thread(void *data)
{
	_music_db_t *mdb = data;
	int i, ret = 0;

        for (i = 0; i < cfg_size(mdb->cfg, "music-dirs"); i++)
	{
		const char *dir = cfg_getnstr(mdb->cfg, "music-dirs", i);
		ret = music_db_scan_directory(mdb, dir);
		if (ret == EINTR) {
			break;
		} else if (ret) {
			log_warning("Failed to scan music directory: %s", dir);
		}
	}

	pthread_mutex_lock(&mdb->scan_mutex);
	mdb->scan_in_progress = 0;
	pthread_mutex_unlock(&mdb->scan_mutex);

	if (ret == EINTR) {
		log_warning("Music collection scan interrupted.");
	} else {
		log_info("Music collection scan complete.");
	}

	pthread_exit(0);
}

#ifdef _DEBUG
static void
_sqlite3_profile(void *d, const char *txt, sqlite3_uint64 time)
{
	(void)d;
	log_trace("[SQLITE3 profile] time: %u ms, statement: %s", time / 1000000, txt);
}
#endif

music_db_t
music_db_init(cfg_t *cfg)
{
	_music_db_t *mdb;
	char *errmsg;

	if (!sqlite3_threadsafe()) {
		log_error("Sqlite3 is not thread safe, terminating!");
		return NULL;
	}

	if (SQLITE_OK != sqlite3_config(SQLITE_CONFIG_SERIALIZED)) {
		log_error("Failed to enable sqlite3 serialization!");
		return NULL;
	}

	if (SQLITE_OK != sqlite3_initialize()) {
		log_error("Failed to initialize sqlite3!");
		return NULL;
	}

	mdb = (_music_db_t *)malloc(sizeof(_music_db_t));
	if (!mdb) {
		log_error("Failed to allocate music database structure!");
		return NULL;
	}
	memset(mdb, 0, sizeof(_music_db_t));

	if (pthread_mutex_init(&mdb->scan_mutex, NULL)) {
		log_error("Failed to initialize scan mutex!");
		free(mdb);
		return NULL;
	}

	if (sqlite3_open(cfg_getstr(cfg, "database-file"), &mdb->db)) {
		log_error("Failed to open database!");
		music_db_shutdown(mdb);
		return NULL;
	}

#ifdef _DEBUG
	sqlite3_profile(mdb->db, _sqlite3_profile, NULL);
#endif

	if (sqlite3_exec(mdb->db, create_basileus_db_str, NULL, NULL, &errmsg)) {
		log_error("Failed to create database: %s!", errmsg);
		sqlite3_free(errmsg);
		music_db_shutdown(mdb);
		return NULL;
	}

	mdb->cfg = cfg;
	mdb->scan_in_progress = 0;
	mdb->scan_terminate = 0;

	return mdb;
}

void
music_db_shutdown(music_db_t mdb)
{
	_music_db_t *_mdb = mdb;

	if (_mdb->scan_in_progress) {
		pthread_mutex_lock(&_mdb->scan_mutex);
		_mdb->scan_terminate = 1;
		pthread_mutex_unlock(&_mdb->scan_mutex);
		pthread_join(_mdb->scan_thread, NULL);
	}

	if (_mdb->db) {
		sqlite3_close(_mdb->db);
		_mdb->db = NULL;
	}

	(void)pthread_mutex_destroy(&_mdb->scan_mutex);

	free(_mdb);

	(void)sqlite3_shutdown();
}

int
music_db_refresh(music_db_t mdb)
{
	_music_db_t *_mdb = mdb;
	int ret = 0;

	if ((ret = pthread_mutex_lock(&_mdb->scan_mutex))) {
		log_error("Failed to lock scan mutex: %d!", ret);
		return 1;
	}

	if (_mdb->scan_in_progress) {
		log_error("Music database scan already in progress.");
		ret = 2;
		goto cleanup;
	}

	if (pthread_create(&_mdb->scan_thread, NULL, &music_db_scan_thread, mdb)) {
		log_error("Failed to create scan thread!");
		ret = 3;
		goto cleanup;
	}

	_mdb->scan_in_progress = 1;
	_mdb->scan_terminate = 0;

cleanup:
	if ((ret = pthread_mutex_unlock(&_mdb->scan_mutex))) {
		log_error("Failed to unlock scan mutex: %d!", ret);
		exit (1);
	}

	return ret;
}

int
_get_artists_cb(void *d, int argc, char **argv, char **column_name)
{
	struct json_object *arr = d;
	struct json_object *artist;

	assert(argc == 1 && argv[0] != NULL);

	artist = json_object_new_string(argv[0]);
	if (artist == NULL) {
		log_error("Failed to allocate artist JSON string!");
		return ENOMEM;
	}

	if (json_object_array_add(arr, artist)) {
		log_error("Failed to add artist to JSON array!");
		json_object_put(artist);
		return ENOMEM;
	}

	return 0;
}

struct json_object *
music_db_get_artists(const music_db_t mdb)
{
	_music_db_t *_mdb = mdb;
	struct json_object *arr;
	char *errmsg;

	arr = json_object_new_array();
	if (arr == NULL) {
		log_error("Failed to allocate JSON artists array!");
		return NULL;
	}

	if (sqlite3_exec(_mdb->db, "SELECT name from artists;", _get_artists_cb, arr, &errmsg)) {
		log_error("Failed to get artist names from database: %s", errmsg);
		sqlite3_free(errmsg);
		json_object_put(arr);
		return NULL;
	}

	return arr;
}

struct json_object *
music_db_get_albums(const music_db_t mdb, const char *artist)
{
	_music_db_t *_mdb = mdb;
	struct json_object *arr = NULL;
	struct json_object *album = NULL;

	arr = json_object_new_array();
	if (arr == NULL) {
		log_error("Failed to allocate json artists array!");
		return NULL;
	}

	sqlite3_stmt *stmt = NULL;
	const char stmt_txt[] = "SELECT name FROM albums WHERE artist_id="
		"(SELECT id FROM artists WHERE name=?);";
	if (SQLITE_OK != sqlite3_prepare_v2(_mdb->db, stmt_txt, -1, &stmt, NULL)) {
		goto failure;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, artist, -1, 0)) {
		goto failure;
	}
	while (1) {
		switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
		case SQLITE_OK:
			goto done;
		case SQLITE_ROW:
		{
			assert(sqlite3_column_type(stmt, 0) == SQLITE_TEXT);
			const char *txt = (const char *)sqlite3_column_text(stmt, 0);
			album = json_object_new_string(txt);
			if (album == NULL) {
				log_error("Failed to create albums JSON string!");
				goto failure;
			}
			if (json_object_array_add(arr, album)) {
				log_error("Failed to add album to JSON array!");
				json_object_put(album);
				goto failure;
			}
			break;
		}
		default:
			log_error("Sqlite3 step failed: %s", sqlite3_errmsg(_mdb->db));
			goto failure;
		}
	}
done:

	sqlite3_finalize(stmt);

	return arr;
failure:
	if (arr) {
		json_object_put(arr);
	}
	if (stmt) {
		sqlite3_finalize(stmt);
	}

	return NULL;
}


struct json_object *
music_db_get_songs(const music_db_t mdb, const char *artist, const char *album)
{
	_music_db_t *_mdb = mdb;
	struct json_object *arr = NULL;
	struct json_object *song = NULL;

	arr = json_object_new_array();
	if (arr == NULL) {
		log_error("Failed to create JSON artists array!");
		return NULL;
	}

	sqlite3_stmt *stmt = NULL;
	const char stmt_txt[] =
		"SELECT title, length, hash FROM songs s "
		"LEFT JOIN artists ar ON s.artist_id=ar.id "
		"LEFT JOIN albums al ON s.album_id=al.id "
		"WHERE al.name=? AND ar.name=? ORDER BY track";
	if (SQLITE_OK != sqlite3_prepare_v2(_mdb->db, stmt_txt, -1, &stmt, NULL)) {
		goto failure;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, album, -1, 0)) {
		goto failure;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 2, artist, -1, 0)) {
		goto failure;
	}
	while (1) {
		switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
		case SQLITE_OK:
			goto done;
		case SQLITE_ROW:
		{
			assert(sqlite3_column_count(stmt) == 3);
			assert(sqlite3_column_type(stmt, 0) == SQLITE_TEXT);
			assert(sqlite3_column_type(stmt, 1) == SQLITE_INTEGER);
			assert(sqlite3_column_type(stmt, 2) == SQLITE_TEXT);

			song = json_object_new_array();
			if (song == NULL) {
				log_error("Failed to create JSON song array!");
				goto failure;
			}

			struct json_object *title = json_object_new_string((const char *)sqlite3_column_text(stmt, 0));
			if (json_object_array_add(song, title)) {
				json_object_put(title);
				goto failure;
			}
			struct json_object *length = json_object_new_int(sqlite3_column_int(stmt, 1));
			if (json_object_array_add(song, length)) {
				json_object_put(length);
				goto failure;
			}
			struct json_object *hash = json_object_new_string((const char *)sqlite3_column_text(stmt, 2));
			if (json_object_array_add(song, hash)) {
				json_object_put(hash);
				goto failure;
			}

			if (json_object_array_add(arr, song)) {
				log_error("Failed to add SONG to JSON array!");
				goto failure;
			}
			song = NULL;
			break;
		}
		default:
			log_error("Sqlite3 step failed: %s", sqlite3_errmsg(_mdb->db));
			goto failure;
		}
	}
done:
	sqlite3_finalize(stmt);

	return arr;

failure:
	if (arr) {
		json_object_put(arr);
	}
	if (song) {
		json_object_put(song);
	}
	if (stmt) {
		sqlite3_finalize(stmt);
	}

	return NULL;
}


char *
music_db_get_song_path(const music_db_t mdb, const char *hash)
{
	_music_db_t *_mdb = mdb;

	sqlite3_stmt *stmt = NULL;
	const char stmt_txt[] = "SELECT path FROM songs WHERE hash=?";
	if (SQLITE_OK != sqlite3_prepare_v2(_mdb->db, stmt_txt, -1, &stmt, NULL)) {
		goto failure;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, hash, -1, 0)) {
		goto failure;
	}
	if (SQLITE_ROW != sqlite3_step(stmt)) {
		goto failure;
	}

	assert(sqlite3_column_type(stmt, 0) == SQLITE_TEXT);

	char *path = strdup((const char *)sqlite3_column_text(stmt, 0));
	if (path == NULL) {
		goto failure;
	}

	assert(sqlite3_step(stmt) == SQLITE_DONE);

	sqlite3_finalize(stmt);

	return path;

failure:
	if (stmt) {
		sqlite3_finalize(stmt);
	}

	return NULL;
}
