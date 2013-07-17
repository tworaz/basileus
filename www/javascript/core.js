function BasileusCore()
{
	POLL_INTERVAL = 10000;

	onconnected = null;
	ondisconnected = null;

	_connected = false;
	_artists = undefined;
	_albums = {};
	_songs = {};
}

BasileusCore.prototype.Poll = function()
{
	var r = new XMLHttpRequest();
	var _thiz = this;
	r.open("GET", "/bctl/status");
	r.onload = function() {
		if (_thiz.onconnected && !_connected) {
			_thiz.onconnected();
		}
		_connected = true;
		window.setTimeout(function() { _thiz.Poll(); }, POLL_INTERVAL);
	}
	r.onerror = function() {
		if (_thiz.ondisconnected) {
			_thiz.ondisconnected();
		}
		_connected = false;
		window.setTimeout(function() { _thiz.Poll(); }, POLL_INTERVAL);
	}
	r.send();
}

BasileusCore.prototype.GetArtists = function(_cb)
{
	var _thiz = this;
	if (_artists != null) {
		_cb(_artists);
	} else {
		var r = new XMLHttpRequest();
		r.open("GET", "/bctl/artists");
		r.onload = function() {
			_artists = JSON.parse(r.responseText);
			_cb(_artists);
		};
		r.error = function() {
			if (_thiz.ondisconnected) {
				_thiz.ondisconnected();
			}
		}
		r.send();
	}
}

BasileusCore.prototype.GetAlbums = function(artist, _cb)
{
	var _thiz = this;
	if (_albums[artist]) {
		_cb(_albums[artist]);
	} else {
		var r = new XMLHttpRequest();
		r.open("GET", "/bctl/albums?artist=" + encodeURIComponent(artist));
		r.onload = function() {
			_albums[artist] = JSON.parse(r.responseText);
			_cb(_albums[artist]);
		}
		r.onerror = function() {
			if (_thiz.ondisconnected) {
				_thiz.ondisconnected();
			}
		}
		r.send();
	}
}

BasileusCore.prototype.GetSongs = function(artist, album, _cb)
{
	var _thiz = this;
	if (_songs[artist] && _songs[artist][album]) {
		_cb(_songs[artist][album]);
	} else {
		var r = new XMLHttpRequest();
		r.open("GET", "/bctl/songs?artist=" + encodeURIComponent(artist) + "&album=" + encodeURIComponent(album));
		r.onload = function() {
			if (_songs[artist] == undefined) {
				_songs[artist] = new Object();
			}
			_songs[artist][album] = JSON.parse(r.responseText);
			_cb(_songs[artist][album]);
		}
		r.onerror = function() {
			if (_thiz.ondisconnected) {
				_thiz.ondisconnected();
			}
		}
		r.send();
	}
}

BasileusCore.prototype.GetSongURI = function(song_id)
{
	return "/stream?song=" + encodeURIComponent(song_id);
}
