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

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <event2/http.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

#include "logger.h"
#include "music_db.h"
#include "webserver.h"

typedef struct {
	cfg_t      *cfg;
	music_db_t *music_db;
	pthread_t   thread;

	const char *doc_root;

	struct event_base          *ev_base;
	struct evhttp              *ev_http;
	struct evhttp_bound_socket *ev_sock;
} _webserver_t;

static const struct table_entry {
	const char *extension;
	const char *content_type;
} content_type_table[] = {
	{ "html", "text/html" },
	{ "htm",  "text/htm" },
	{ "css",  "text/css" },
	{ "gif",  "image/gif" },
	{ "jpg",  "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "png",  "image/png" },
	{ "ico",  "image/x-icon" },
	{ "svg",  "image/svg+xml" },
	{ "js",   "application/javascript" },
	{ "eot",  "application/vnd.ms-fontobject" },
	{ "woff", "application/font-woff" },
	{ "mp3",  "audio/mpeg" },
	{ "ogg",  "application/ogg" },
	{ "ogx",  "application/ogx" },
	{ NULL,   NULL },
};

static const char *
_guess_content_type(const char *path)
{
	const char *last_period, *extension;
	const struct table_entry *ent;
	last_period = strrchr(path, '.');
	if (!last_period || strchr(last_period, '/'))
		goto not_found;
	extension = last_period + 1;
	for (ent = &content_type_table[0]; ent->extension; ++ent) {
		if (!evutil_ascii_strcasecmp(ent->extension, extension))
			return ent->content_type;
	}

not_found:
#ifdef _DEBUG
	log_warning("No MIME type for: %s", extension);
#endif /* _DEBUG */
	return "application/octet-stream";
}

static int
_send_json(struct evhttp_request *req, struct json_object *json)
{
	struct evbuffer *buf = NULL;
	int ret = 0;

	const char *json_str = json_object_get_string(json);
	if (NULL == json_str) {
		goto error;
	}

	if (NULL == (buf = evbuffer_new())) {
		goto error;
	}

	if (0 != evhttp_add_header(evhttp_request_get_output_headers(req),
	                           "Content-Type", "application/json")) {
		goto error;
	}

	int len = strlen(json_str);
	if (0 != evbuffer_add(buf, json_str, len)) {
		goto error;
	}

	evhttp_send_reply(req, 200, "OK", buf);

	goto done;

error:
	ret = -1;
done:
	if (buf) {
		evbuffer_free(buf);
	}
	return ret;
}

static int
_send_file(struct evhttp_request *req, const char *path)
{
	struct evkeyvalq *in_headers = evhttp_request_get_input_headers(req);
	struct evkeyvalq *out_headers = evhttp_request_get_output_headers(req);
	struct evbuffer *buf = NULL;
	int ret = 0, fd = -1;
	int content_length = 0;
	struct stat st;

	if (NULL == (buf = evbuffer_new())) {
		goto error;
	}

	if ((fd = open(path, O_RDONLY)) < 0) {
		log_error("Failed to open file %s: %d", path, errno);
		goto error;
	}

	if (0 != fstat(fd, &st)) {
		log_error("Failed to stat content file %s: %d", path, errno);
		goto error;
	}

	const char *type = _guess_content_type(path);
	if (0 != evhttp_add_header(out_headers, "Content-Type", type)) {
		goto error;
	}

	content_length = st.st_size;
	const char *range = evhttp_find_header(in_headers, "range");
	if (range) {
		int64_t start = 0, end = 0;
		int matched = sscanf(range, "bytes=%" PRId64 "-%" PRId64, &start, &end);
		if (matched == 1 && start == 0) {
			end = st.st_size;
		} else {
			content_length = end - start;
		}
		char rb[79]; /* 3*20 (int64) + 9 */
		memset(rb, 0, sizeof(rb));
		snprintf(rb, sizeof(rb), "bytes %" PRId64 "-%" PRId64 "/%" PRId64,
		         start, start + content_length - 1, st.st_size);

		if (0 != evhttp_add_header(out_headers, "Content-Range", rb)) {
			goto error;
		}

		if (0 != evbuffer_add_file(buf, fd, start, content_length)) {
			goto error;
		}

		evhttp_send_reply(req, 206, "Partial Content", buf);
	} else {
		if (0 != evbuffer_add_file(buf, fd, 0, st.st_size)) {
			goto error;
		}
		evhttp_send_reply(req, 200, "OK", buf);
	}

	goto done;

error:
	ret = -1;
	if (fd >= 0) {
		close(fd);
	}
done:
	if (buf) {
		evbuffer_free(buf);
	}
	return ret;
}

static void
_status_request(struct evhttp_request *req, void *arg)
{
	struct evbuffer *buf = NULL;

	if (NULL == (buf = evbuffer_new())) {
		goto error;
	}

	if (0 >= evbuffer_add_printf(buf, "Alive")) {
		goto error;
	}

	evhttp_send_reply(req, 200, "OK", buf);

	goto done;

error:
	evhttp_send_error(req, 500, "Internal Server Error");
	log_error("Failed to service server status request");
done:
	if (buf) {
		evbuffer_free(buf);
	}
}

static void
_artists_request(struct evhttp_request *req, void *arg)
{
	_webserver_t *ws = arg;

	log_trace("Got artists listing request");

	struct json_object *artists = music_db_get_artists(ws->music_db);
	if (artists == NULL) {
		goto error;
	}

	if (0 != _send_json(req, artists)) {
		goto error;
	}

	goto done;

error:
	evhttp_send_error(req, 500, "Internal Server Error");
	log_error("Failed to service artists listing request!");
done:
	if (artists) {
		json_object_put(artists);
	}
	return;
}

static void
_albums_request(struct evhttp_request *req, void *arg)
{
	struct json_object *albums = NULL;
	_webserver_t *ws = arg;
	struct evhttp_uri *uri;
	const char *query_str = NULL;
	struct evkeyvalq q;

	uri = evhttp_uri_parse(evhttp_request_get_uri(req));
	if (NULL == uri) {
		goto error;
	}

	query_str = evhttp_uri_get_query(uri);
	if (NULL == query_str) {
		goto error;
	}

	if (0 != evhttp_parse_query_str(query_str, &q)) {
		goto error;
	}

	const char *artist = evhttp_find_header(&q, "artist");
	if (NULL == artist) {
		goto error;
	}

	log_trace("Got albums listing request, artist: \"%s\"", artist);

	albums = music_db_get_albums(ws->music_db, artist);
	if (NULL == albums) {
		goto error;
	}

	if (0 != _send_json(req, albums)) {
		goto error;
	}

	goto done;

error:
	evhttp_send_error(req, HTTP_BADREQUEST, "Bad request");
	log_error("Failed to service albums listing request!");
done:
	evhttp_clear_headers(&q);
	if (uri) {
		evhttp_uri_free(uri);
	}
	if (albums) {
		json_object_put(albums);
	}
	return;
}

static void
_songs_request(struct evhttp_request *req, void *arg)
{
	struct json_object *songs = NULL;
	_webserver_t *ws = arg;
	struct evhttp_uri *uri;
	const char *query_str = NULL;
	struct evkeyvalq q;

	uri = evhttp_uri_parse(evhttp_request_get_uri(req));
	if (NULL == uri) {
		goto error;
	}

	query_str = evhttp_uri_get_query(uri);
	if (NULL == query_str) {
		goto error;
	}

	if (0 != evhttp_parse_query_str(query_str, &q)) {
		goto error;
	}

	const char *artist = evhttp_find_header(&q, "artist");
	if (NULL == artist) {
		goto error;
	}

	const char *album = evhttp_find_header(&q, "album");
	if (NULL == album) {
		goto error;
	}

	log_trace("Got songs listing request, artist: \"%s\", album: \"%s\"", artist, album);

	songs = music_db_get_songs(ws->music_db, artist, album);
	if (songs == NULL) {
		goto error;
	}

	if (0 != _send_json(req, songs)) {
		goto error;
	}

	goto done;

error:
	evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
	log_error("Failed to service songs listing request!");
done:
	evhttp_clear_headers(&q);
	if (uri) {
		evhttp_uri_free(uri);
	}
	if (songs) {
		json_object_put(songs);
	}
	return;
}

static void
_stream_request(struct evhttp_request *req, void *arg)
{
	_webserver_t *ws = arg;
	struct evhttp_uri *uri;
	const char *query_str = NULL;
	char *song_path = NULL;
	struct evkeyvalq q;

	uri = evhttp_uri_parse(evhttp_request_get_uri(req));
	if (NULL == uri) {
		goto error;
	}

	query_str = evhttp_uri_get_query(uri);
	if (NULL == query_str) {
		goto error;
	}

	if (0 != evhttp_parse_query_str(query_str, &q)) {
		goto error;
	}

	const char *song = evhttp_find_header(&q, "song");
	if (NULL == song) {
		goto error;
	}

	song_path = music_db_get_song_path(ws->music_db, song);
	if (song_path == NULL) {
		goto error;
	}

	log_trace("Got streaming request for song: %s", song_path);

	if (0 != _send_file(req, song_path)) {
		goto error;
	}

	goto done;

error:
	evhttp_send_error(req, HTTP_BADREQUEST, "Bad request");
	log_error("Failed to service songs streaming request!");
done:
	evhttp_clear_headers(&q);
	if (uri) {
		evhttp_uri_free(uri);
	}
	if (song_path) {
		free(song_path);
	}
	return;
}

static const struct {
	const char  *path;
	void       (*callback) (struct evhttp_request *, void *);
} request_table[] = {
	{ "/bctl/status",  _status_request  },
	{ "/bctl/artists", _artists_request },
	{ "/bctl/albums",  _albums_request  },
	{ "/bctl/songs",   _songs_request   },
	{ "/stream",       _stream_request  },
};

static void
_document_request(struct evhttp_request *req, void *arg)
{
	_webserver_t *ws = arg;
	const char *uri = evhttp_request_get_uri(req);
	struct evhttp_uri *decoded = NULL;
	struct stat st;
	const char *path;
	char *full_path = NULL;
	char *decoded_path;
	int len;

	log_trace("Content request: %s", uri);

	decoded = evhttp_uri_parse(uri);
	if (!decoded) {
		log_warning("Got bad URI request!");
		evhttp_send_error(req, HTTP_BADREQUEST, 0);
		return;
	}

	path = evhttp_uri_get_path(decoded);
	if (!path) {
		path = "/";
	} else if (strlen(path) == 1) {
		path = "index.html";
	}

	decoded_path = evhttp_uridecode(path, 0, NULL);
	if (decoded_path == NULL)
		goto error;

	if (strstr(decoded_path, ".."))
		goto error;

	len = strlen(decoded_path) + strlen(ws->doc_root) + 2;
	if (!(full_path = malloc(len))) {
		log_error("Failed to allocate memory for full document path!");
		goto error;
	}
	evutil_snprintf(full_path, len, "%s/%s", ws->doc_root, decoded_path);

	if (0 != stat(full_path, &st)) {
		log_warning("Requested content does not exist: %s", full_path);
		goto error;
	}

	if (S_ISDIR(st.st_mode)) {
		goto error;
	}

	if (0 != _send_file(req, full_path)) {
		goto error;
	}

	goto cleanup;

error:
	evhttp_send_error(req, 404, "Document not found");

cleanup:
	if (decoded) {
		evhttp_uri_free(decoded);
	}
	if (decoded_path) {
		free(decoded_path);
	}
	if (full_path) {
		free(full_path);
	}
}

static void *
_webserver_thread(void *data)
{
	_webserver_t *ws = data;

	log_info("Web server thread started");

	event_base_dispatch(ws->ev_base);

	log_info("Web server thread exiting...");

	pthread_exit(0);
}

webserver_t
webserver_init(cfg_t *cfg, music_db_t *db)
{
	_webserver_t *ws;
	int i;

	if (0 != evthread_use_pthreads()) {
		log_error("Failed to instruct libevent to use pthreads!");
		return NULL;
	}

	ws = malloc(sizeof(_webserver_t));
	if (ws == NULL) {
		log_error("Failed to allocate memory for webserver!");
		return NULL;
	}
	memset(ws, 0, sizeof(_webserver_t));

	ws->ev_base = event_base_new();
	if (!ws->ev_base) {
		log_error("Failed to create event_base!");
		goto failure;
	}

	ws->ev_http = evhttp_new(ws->ev_base);
	if (!ws->ev_http) {
		log_error("Failed to create evhttp!");
		goto failure;
	}

	for (i = 0; i < sizeof(request_table) / sizeof(request_table[0]); i++) {
		log_trace("Registering callback for: %s", request_table[i].path);
		if (0 != evhttp_set_cb(ws->ev_http, request_table[i].path, request_table[i].callback, ws)) {
			log_error("Failed to register control callback!");
			goto failure;
		}
	}

	evhttp_set_allowed_methods(ws->ev_http, EVHTTP_REQ_GET);
	evhttp_set_gencb(ws->ev_http, _document_request, ws);

	const char *address = cfg_get_str(cfg, CFG_LISTENING_ADDRESS);
	int port = atoi(cfg_get_str(cfg, CFG_LISTENING_PORT));

	ws->ev_sock = evhttp_bind_socket_with_handle(ws->ev_http, address, port);
	if (!ws->ev_sock) {
		log_error("Failed to bind to port!");
		goto failure;
	}

	if (pthread_create(&ws->thread, NULL, &_webserver_thread, ws)) {
		log_error("Failed to createwebserver thread!");
		goto failure;
	}

	ws->cfg = cfg;
	ws->music_db = db;
	ws->doc_root = cfg_get_str(cfg, CFG_DOCUMENT_ROOT);

	log_info("Web server started (using libevent %s)", event_get_version());

	return ws;

failure:
	if (ws->ev_http) {
		evhttp_free(ws->ev_http);
	}
	if (ws->ev_base) {
		event_base_free(ws->ev_base);
	}
	free(ws);
	return NULL;
}

void
webserver_shutdown(webserver_t ws)
{
	_webserver_t *_ws = ws;

	if (0 != event_base_loopexit(_ws->ev_base, NULL)) {
		log_error("Failed to cleanly terminate web server!");
		return;
	}
	pthread_join(_ws->thread, NULL);

	evhttp_free(_ws->ev_http);
	event_base_free(_ws->ev_base);
	free(_ws);
}
