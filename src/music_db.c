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

#include <sqlite3.h>

#include "cfg.h"
#include "md5.h"
#include "logger.h"
#include "music_db.h"
#include "music_tag.h"
#include "basileus-music-db.h"

typedef struct {
	sqlite3	        *db;
	sqlite3_mutex	*db_mutex;

	cfg_t	        *cfg;
	scheduler_t     *scheduler;

	pthread_mutex_t	 scan_mutex;
	pthread_t        scan_thread;
	int              scan_in_progress : 1;
	int              scan_terminate : 1;
} _music_db_t;

static int
_music_db_add_artist(_music_db_t *mdb, const char *artist, sqlite3_int64 *out_id)
{
	sqlite3_stmt *stmt = NULL;
	int ret = -1;

	sqlite3_mutex_enter(mdb->db_mutex);

	const char stmt_txt[] = "INSERT INTO artists (name) VALUES (?);";
	if (SQLITE_OK != sqlite3_prepare_v2(mdb->db, stmt_txt, -1, &stmt, NULL)) {
		log_error("Failed to prepare sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, artist, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_DONE != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		stmt = NULL;
		goto finish;
	}
	stmt = NULL;

	const char stmt_txt2[] = "SELECT id from artists WHERE name=?;";
	if (SQLITE_OK != sqlite3_prepare_v2(mdb->db, stmt_txt2, -1, &stmt, NULL)) {
		log_error("Failed to prepare sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, artist, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_ROW != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (sqlite3_column_count(stmt) != 1 || sqlite3_column_type(stmt, 0) != SQLITE_INTEGER) {
		log_error("Failed to get newly added artist id!");
		goto finish;
	}
	*out_id = sqlite3_column_int(stmt, 0);
	if (SQLITE_DONE != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		stmt = NULL;
		goto finish;
	}

	stmt = NULL;
	ret = 0;

finish:
	sqlite3_finalize(stmt);
	sqlite3_mutex_leave(mdb->db_mutex);

	return ret;
}

static int
_music_db_add_album(_music_db_t *mdb, const char *album, sqlite3_int64 artist_id, sqlite3_int64 *out_id)
{
	sqlite3_stmt *stmt = NULL;
	int ret = -1;

	sqlite3_mutex_enter(mdb->db_mutex);

	const char stmt_txt[] = "INSERT INTO albums (name, artist_id) VALUES (?, ?);";
	if (SQLITE_OK != sqlite3_prepare_v2(mdb->db, stmt_txt, -1, &stmt, NULL)) {
		log_error("Failed to prepare sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, album, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_int(stmt, 2, artist_id)) {
		log_error("Failed to bind statement integer: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_DONE != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		stmt = NULL;
		goto finish;
	}
	stmt = NULL;

	const char stmt_txt2[] = "SELECT id from albums WHERE name=? AND artist_id=?;";
	if (SQLITE_OK != sqlite3_prepare_v2(mdb->db, stmt_txt2, -1, &stmt, NULL)) {
		log_error("Failed to prepare sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, album, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_int(stmt, 2, artist_id)) {
		log_error("Failed to bind statement integer: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_ROW != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (sqlite3_column_count(stmt) != 1 || sqlite3_column_type(stmt, 0) != SQLITE_INTEGER) {
		log_error("Failed to get newly added artist id!");
		goto finish;
	}
	*out_id = sqlite3_column_int(stmt, 0);
	if (SQLITE_DONE != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		stmt = NULL;
		goto finish;
	}
	stmt = NULL;

	ret = 0;

finish:
	sqlite3_finalize(stmt);
	sqlite3_mutex_leave(mdb->db_mutex);

	return ret;
}

static int
_music_db_add_song(_music_db_t *mdb, const char *path, music_tag_t *tag, sqlite3_int64 artist_id, sqlite3_int64 album_id)
{
	sqlite3_stmt *stmt = NULL;
	char hash[33];
	int ret = -1;

	memset(hash, 0, sizeof(hash));
	md5(hash, path, NULL);

	sqlite3_mutex_enter(mdb->db_mutex);

	const char stmt_txt[] = "INSERT INTO songs (title, path, hash, track, length, artist_id, album_id) "
	                        "VALUES (:1, :2, :3, :4, :5, :6, :7);";
	if (SQLITE_OK != sqlite3_prepare_v2(mdb->db, stmt_txt, -1, &stmt, NULL)) {
		log_error("Failed to prepare sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 1, tag->title, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 2, path, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_text(stmt, 3, hash, -1, 0)) {
		log_error("Failed to bind statement text: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_int(stmt, 4, tag->track)) {
		log_error("Failed to bind statement integer: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_int(stmt, 5, tag->length)) {
		log_error("Failed to bind statement integer: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_int(stmt, 6, artist_id)) {
		log_error("Failed to bind statement integer: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_bind_int(stmt, 7, album_id)) {
		log_error("Failed to bind statement integer: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_DONE != sqlite3_step(stmt)) {
		log_error("Failed to step sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		goto finish;
	}
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(mdb->db));
		stmt = NULL;
		goto finish;
	}

	stmt = NULL;
	ret = 0;

finish:
	sqlite3_finalize(stmt);
	sqlite3_mutex_leave(mdb->db_mutex);

	return ret;
}

static int
music_db_add_file(_music_db_t *mdb, const char *path)
{
	sqlite3_int64 artist_id, album_id;
	music_tag_t *tag = NULL;
	int ret = -1;

	tag = music_tag_create(path);
	if (tag == NULL) {
		log_debug("No audio metadata found in: %s", path);
		return 0;
	}

	log_trace("Adding file to database: %s", path);

	if (0 != _music_db_add_artist(mdb, tag->artist, &artist_id)) {
		log_error("Failed to add artist \"%s\" to music database!", tag->artist);
		goto finish;
	}
	if (0 != _music_db_add_album(mdb, tag->album, artist_id, &album_id)) {
		log_error("Failed to add album \"%s\" to music database!", tag->album);
		goto finish;
	}
	if (0 != _music_db_add_song(mdb, path, tag, artist_id, album_id)) {
		log_error("Failed to add song \"%s\" to music database!", tag->title);
		goto finish;
	}

	ret = 0;

finish:
	music_tag_destroy(tag);
	return ret;
}

static int
music_db_scan_directory(_music_db_t *mdb, const char *dir)
{
	struct dirent *entry = NULL, *dirent_buf = NULL;
	DIR *dirp = NULL;;
	struct stat st;
	char *full_path = NULL;
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

static void
_music_db_scan_finished(void *data)
{
	_music_db_t *_mdb = data;
	log_debug("Scan finished, joining scan thread");
	pthread_join(_mdb->scan_thread, NULL);
}

static void *
music_db_scan_thread(void *data)
{
	_music_db_t *mdb = data;
	int ret = 0;

	const char *dir = cfg_get_str(mdb->cfg, CFG_MUSIC_DIR);
	log_info("Scanning music directory: %s", dir);
	ret = music_db_scan_directory(mdb, dir);
	if (ret && ret != EINTR) {
		log_warning("Failed to scan music directory: %s", dir);
	}

	pthread_mutex_lock(&mdb->scan_mutex);
	mdb->scan_in_progress = 0;
	pthread_mutex_unlock(&mdb->scan_mutex);

	if (ret == EINTR) {
		log_warning("Music collection scan interrupted.");
	} else {
		log_info("Music collection scan complete.");
	}

	event_t *e = malloc(sizeof(event_t));
	if (e == NULL) {
		log_error("Failed to allocate memory for event_t!");
		pthread_exit(0);
	}
	memset(e, 0, sizeof(event_t));

	e->name = "Music database scan finished";
	e->run = _music_db_scan_finished;
	e->user_data = mdb;
	if (0 != scheduler_add_event(mdb->scheduler, e)) {
		log_error("Failed to schedule new event!");
		free(e);
	}

	pthread_exit(0);
}

#ifdef SQLITE3_PROFILE
static void
_sqlite3_profile(void *d, const char *txt, sqlite3_uint64 time)
{
	(void)d;
	log_trace("[SQLITE3 profile] time: %u ms, statement: %s", time / 1000000, txt);
}
#endif

music_db_t
music_db_new(cfg_t *cfg, scheduler_t *sched)
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

	if (NULL == (mdb->db_mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_RECURSIVE))) {
		log_error("Failed to allocate sqlite3 mutex!");
		music_db_free(mdb);
		return NULL;
	}

	if (SQLITE_OK != sqlite3_open(cfg_get_str(cfg, CFG_DATABASE_PATH), &mdb->db)) {
#ifdef _DEBUG
		sqlite3_close(mdb->db);
		if (SQLITE_OK == sqlite3_open("basileus-dev.sqlite3", &mdb->db)) {
			goto devdb;
		}
#endif
		log_error("Failed to open database!");
		music_db_free(mdb);
		return NULL;
	}

#ifdef _DEBUG
devdb:
#endif

#ifdef SQLITE3_PROFILE
	sqlite3_profile(mdb->db, _sqlite3_profile, NULL);
#endif

	if (sqlite3_exec(mdb->db, create_basileus_db_str, NULL, NULL, &errmsg)) {
		log_error("Failed to create database: %s!", errmsg);
		sqlite3_free(errmsg);
		music_db_free(mdb);
		return NULL;
	}

	mdb->cfg = cfg;
	mdb->scheduler = sched;
	mdb->scan_in_progress = 0;
	mdb->scan_terminate = 0;

	return mdb;
}

void
music_db_free(music_db_t mdb)
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

	if (_mdb->db_mutex) {
		sqlite3_mutex_free(_mdb->db_mutex);
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

	if (0 != (ret = pthread_mutex_lock(&_mdb->scan_mutex))) {
		log_error("Failed to lock scan mutex: %d!", ret);
		return ret;
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

	sqlite3_mutex_enter(_mdb->db_mutex);

	arr = json_object_new_array();
	if (arr == NULL) {
		log_error("Failed to allocate JSON artists array!");
		goto finish;
	}

	if (sqlite3_exec(_mdb->db, "SELECT name from artists;", _get_artists_cb, arr, &errmsg)) {
		log_error("Failed to get artist names from database: %s", errmsg);
		sqlite3_free(errmsg);
		json_object_put(arr);
		arr = NULL;
		goto finish;
	}

finish:
	sqlite3_mutex_leave(_mdb->db_mutex);
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

	sqlite3_mutex_enter(_mdb->db_mutex);

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
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(_mdb->db));
		stmt = NULL;
		goto failure;
	}

	sqlite3_mutex_leave(_mdb->db_mutex);
	return arr;

failure:
	if (arr) {
		json_object_put(arr);
	}
	sqlite3_finalize(stmt);
	sqlite3_mutex_leave(_mdb->db_mutex);

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

	sqlite3_mutex_enter(_mdb->db_mutex);

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

			song = json_object_new_object();
			if (song == NULL) {
				log_error("Failed to create JSON song array!");
				goto failure;
			}

			struct json_object *title = json_object_new_string((const char *)sqlite3_column_text(stmt, 0));
			if (NULL == title) {
				goto failure;
			}
			json_object_object_add(song, "title", title);

			struct json_object *length = json_object_new_int(sqlite3_column_int(stmt, 1));
			if (NULL == length) {
				goto failure;
			}
			json_object_object_add(song, "length", length);

			struct json_object *hash = json_object_new_string((const char *)sqlite3_column_text(stmt, 2));
			if (NULL == hash) {
				goto failure;
			}
			json_object_object_add(song, "hash", hash);

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
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(_mdb->db));
		stmt = NULL;
		goto failure;
	}

	sqlite3_mutex_leave(_mdb->db_mutex);

	return arr;

failure:
	if (arr) {
		json_object_put(arr);
	}
	if (song) {
		json_object_put(song);
	}
	sqlite3_finalize(stmt);
	sqlite3_mutex_leave(_mdb->db_mutex);

	return NULL;
}


char *
music_db_get_song_path(const music_db_t mdb, const char *hash)
{
	_music_db_t *_mdb = mdb;
	sqlite3_stmt *stmt = NULL;
	char *path = NULL;

	sqlite3_mutex_enter(_mdb->db_mutex);

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

	path = strdup((const char *)sqlite3_column_text(stmt, 0));

	assert(sqlite3_step(stmt) == SQLITE_DONE);

failure:
	if (SQLITE_OK != sqlite3_finalize(stmt)) {
		log_error("Failed to finalize sqlite3 statement: %s", sqlite3_errmsg(_mdb->db));
		stmt = NULL;
		goto failure;
	}
	sqlite3_mutex_leave(_mdb->db_mutex);

	return path;
}
