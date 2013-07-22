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

#include <stdlib.h>
#include <string.h>

#include "webserver.h"
#include "mongoose.h"
#include "logger.h"

typedef struct {
	struct mg_context   *ctx;
	struct mg_callbacks  cbs;
	cfg_t               *cfg;
	music_db_t          *music_db;
} _webserver_t;

#define MAX_QUERY_LENGTH 1024

#define ERR_QUERY_TOO_LONG "HTTP/1.1 400 Bad Request\r\n \
                           Content-Type: text/plain\r\n \
			   Content-Length: 21\r\n \
			   \r\n \
			   Query string too long"

#define ERR_GENERIC "HTTP/1.1 500 Internal Server Error\r\n"

static int
_log_message(const struct mg_connection *conn, const char *message)
{
	(void)conn;
	log_info(message);
	return 1;
}

static int
_handle_get_artists(struct mg_connection *conn, music_db_t *mdb)
{
	struct json_object *artists = music_db_get_artists(mdb);
	if (artists == NULL) {
		goto failure;
	}

	const char *artists_str = json_object_get_string(artists);
	if (artists_str == NULL) {
		goto failure;
	}

	int len = strlen(artists_str);
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			"Content-Type: application/json\r\n"
			"Content-length: %d\r\n\r\n", len);
	mg_write(conn, artists_str, len);

	json_object_put(artists);
	return 1;
failure:
	if (artists) {
		json_object_put(artists);
	}
	mg_printf(conn, ERR_GENERIC);
	return 1;

}

static int
_handle_get_albums(struct mg_connection *conn, music_db_t *mdb, const char *query)
{
	char decode_buf[MAX_QUERY_LENGTH];
	const char *err_str = ERR_GENERIC;

	if (mg_get_var(query, strlen(query), "artist", decode_buf, sizeof(decode_buf)) <= 0) {
		err_str = ERR_QUERY_TOO_LONG;
		goto failure;
	}

	struct json_object *albums = music_db_get_albums(mdb, decode_buf);
	if (albums == NULL) {
		goto failure;
	}

	const char *albums_str = json_object_get_string(albums);
	if (albums_str == NULL) {
		goto failure;
	}

	int len = strlen(albums_str);
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			"Content-Type: application/json\r\n"
			"Content-length: %d\r\n\r\n", len);
	mg_write(conn, albums_str, len);
	json_object_put(albums);

	return 1;
failure:
	if (albums) {
		json_object_put(albums);
	}
	mg_printf(conn, "%s", err_str);

	return 1;
}

static int
_handle_get_songs(struct mg_connection *conn, music_db_t *mdb, const char *query)
{
	char artist_buf[MAX_QUERY_LENGTH];
	char album_buf[MAX_QUERY_LENGTH];
	const char *err_str = ERR_GENERIC;

	if (mg_get_var(query, strlen(query), "artist", artist_buf, sizeof(artist_buf)) <= 0) {
		err_str = ERR_QUERY_TOO_LONG;
		goto failure;
	}
	if (mg_get_var(query, strlen(query), "album", album_buf, sizeof(album_buf)) <= 0) {
		err_str = ERR_QUERY_TOO_LONG;
		goto failure;
	}

	struct json_object *songs = music_db_get_songs(mdb, artist_buf, album_buf);
	if (songs == NULL) {
		goto failure;
	}

	const char *songs_str = json_object_get_string(songs);
	if (songs_str == NULL) {
		goto failure;
	}

	int len = strlen(songs_str);
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			"Content-Type: application/json\r\n"
			"Content-length: %d\r\n\r\n", len);
	mg_write(conn, songs_str, len);
	json_object_put(songs);

	return 1;
failure:
	if (songs) {
		json_object_put(songs);
	}
	mg_printf(conn, "%s", err_str);

	return 1;
}

static int
_handle_stream(struct mg_connection *conn, music_db_t *mdb, const char *query)
{
	char decode_buf[MAX_QUERY_LENGTH];
	char *path = NULL;
	const char* err_str = ERR_GENERIC;

	if (mg_get_var(query, strlen(query), "song", decode_buf, sizeof(decode_buf)) <= 0) {
		err_str = ERR_QUERY_TOO_LONG;
		goto failure;
	}

	path = music_db_get_song_path(mdb, decode_buf);
	if (path == NULL) {
		goto failure;
	}

	mg_send_file(conn, path);

	return 1;
failure:
	if (path) {
		free(path);
	}
	mg_printf(conn, "%s", err_str);

	return 1;
}

static int
_begin_request(struct mg_connection *conn)
{
	const struct mg_request_info *rqi = mg_get_request_info(conn);
	const _webserver_t *ws = rqi->user_data;

	if (strncmp(rqi->uri, "/bctl/status", 12) == 0) {
		mg_printf(conn, "HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\n"
		                "Content-length: 5\r\n\r\n"
				"Alive");
		return 1;
	} else if (strncmp(rqi->uri, "/bctl/artists", 13) == 0) {
		return _handle_get_artists(conn, ws->music_db);
	} else if (strncmp(rqi->uri, "/bctl/albums", 12) == 0) {
		return _handle_get_albums(conn, ws->music_db, rqi->query_string);
	} else if (strncmp(rqi->uri, "/bctl/songs", 11) == 0) {
		return _handle_get_songs(conn, ws->music_db, rqi->query_string);
	} else if (strncmp(rqi->uri, "/stream", 7) == 0) {
		return _handle_stream(conn, ws->music_db, rqi->query_string);
	} else {
		log_trace("File request: %s", rqi->uri);
		return 0;
	}

	return 0;
}

webserver_t
webserver_init(cfg_t *cfg, music_db_t *db)
{
	_webserver_t *ws;

	ws = malloc(sizeof(_webserver_t));
	if (ws == NULL) {
		log_error("Failed to allocate memory for webserver!");
		return NULL;
	}
	memset(ws, 0, sizeof(_webserver_t));

	ws->cbs.begin_request = _begin_request;
	ws->cbs.log_message = _log_message;

	const char *mg_opts[] = {
		"listening_ports", cfg_getstr(cfg, "listening-ports"),
		"document_root", cfg_getstr(cfg, "document-root"),
		"num_threads", cfg_getstr(cfg, "mongoose-threads"),
		"enable_directory_listing", "no",
		"enable_keep_alive", "no", /* Broken in mongoose 3.7 */
		NULL
	};

	ws->cfg = cfg;
	ws->music_db = db;

	ws->ctx = mg_start(&ws->cbs, ws, mg_opts);
	if (ws->ctx == NULL) {
		log_error("Failed to start mongoose!");
		free(ws);
		return NULL;
	}

	log_info("Mongoose %s started", mg_version());

	return ws;
}

void
webserver_shutdown(webserver_t ws)
{
	_webserver_t *_ws = ws;
	mg_stop(_ws->ctx);
	free(_ws);
}
