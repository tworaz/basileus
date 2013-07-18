function BasileusUI()
{
	PLAY_TIMEOUT = 750;
	PROGRESS_BAR_UPDATE_INTERVAL = 1000;

	_core = new BasileusCore();
	_thiz = this;

	_core.onconnected = function(txt)
	{
		document.getElementById('server-status').textContent = "Connected";
	}

	_core.ondisconnected = function()
	{
		document.getElementById('server-status').textContent = "Disconnected";
	}

	_core.Poll();

	_selectedArtist = null;

	_player = undefined;
	_playlist = [];
	_currentSong = undefined;

	_play_timer = null;
	_progress_bar_timer = null;

	_repeat = false;
	_random = false;
}

BasileusUI.prototype.OnLoad = function()
{
	var _thiz = this;
	_player = document.getElementById("audio-player");

	_player.addEventListener('ended', function(evt) {
		console.log("Media event received: ended");
		_thiz.UpdateProgressBar();
		_thiz.Next(true);
	});
	_player.addEventListener('play', function(evt) {
		console.log("Media event received: play");
		_thiz.UpdatePlayPauseButton();
		_thiz.UpdateNowPlaying();
		_thiz.UpdateProgressBar();
		if (_progress_bar_timer) {
			window.clearInterval(_progress_bar_timer);
		}
		_progress_bar_timer = window.setInterval(function() {
			_thiz.UpdateProgressBar();
		}, PROGRESS_BAR_UPDATE_INTERVAL);
	});
	_player.addEventListener('pause', function(evt) {
		console.log("Media event received: pause");
		_thiz.UpdatePlayPauseButton();
		window.clearInterval(_progress_bar_timer);
		_progress_bar_timer = undefined;
	});
	_player.addEventListener('emtied', function(evt) {
		console.log("Media event received: emptied");
	});
	_player.addEventListener('progress', function(evt) {
		console.log("Media event received: progress");
		_thiz.UpdateBufferedBar();
	});
	_player.addEventListener('loadeddata', function(evt) {
		console.log("Media event received: loadeddata");
		_thiz.UpdateBufferedBar();
	});
	_player.addEventListener('durationchange', function(evt) {
		console.log("Media event received: durationchange");
		_thiz.UpdateProgressBar();
	});
	_player.addEventListener('abort', function(evt) {
		console.log("Media event received: abort");
		if (_progress_bar_timer) {
			window.clearInterval(_progress_bar_timer);
			_progress_bar_timer = undefined;
		}
		if (evt.target.error) {
			_thiz.HandleMediaError(evt.target.error);
		}
	});
	_player.addEventListener('error', function(evt) {
		console.log("Media event received: error");
		if (_progress_bar_timer) {
			window.clearInterval(_progress_bar_timer);
			_progress_bar_timer = undefined;
		}
		_thiz.HandleMediaError(evt.target.error);
	});

	var bar = document.getElementById('progress-bar-outline');
	bar.addEventListener('click', function(evt) {
		_thiz.SeekTo(evt.clientX);
	});
	bar.addEventListener('mouseover', function(evt) {
		if (_thiz.PreviewPosition(evt.clientX)) {
			document.getElementById('seek-preview').style.display = "inline";
		}
	});
	bar.addEventListener('mouseout', function(evt) {
		document.getElementById('seek-preview').style.display = "none";
	});
	bar.addEventListener('mousemove', function(evt) {
		_thiz.PreviewPosition(evt.clientX);
	});

	window.addEventListener('resize', function() {
		_thiz.UpdateBufferedBar();
	});

	this.ShowArtistsList();
}

BasileusUI.prototype.ShowArtistsList = function()
{
	_core.GetArtists(function(artists) {
		var list = document.getElementById("media-browser");
		var hdr = document.getElementById("media-browser-header");

		hdr.textContent = "Artists Browser";

		while (list.lastChild) {
			list.removeChild(list.lastChild);
		}

		for (var i = 0; i < artists.length; i++) {
			var item = document.createElement("div");
			var text = document.createElement("div");
			var txt = document.createTextNode(artists[i]);

			if (i % 2) {
				item.className = "media-browser-item rounded-box odd-item";
			} else {
				item.className = "media-browser-item rounded-box even-item";
			}
			text.className = "media-browser-item-text";

			text.appendChild(txt);
			item.appendChild(text);

			item.addEventListener("click", function() {
				_thiz.SelectArtist(this.textContent);
			});

			list.appendChild(item);
		}
	});
}

BasileusUI.prototype.SelectArtist = function(artist)
{
	_selectedArtist = artist;
	_core.GetAlbums(artist, function(albums) {
		var list = document.getElementById("media-browser");
		var hdr = document.getElementById("media-browser-header");

		hdr.textContent = artist;

		while (list.lastChild) {
			list.removeChild(list.lastChild);
		}

		var up = document.createElement("div");
		var up_txt = document.createElement("div");
		up_txt.textContent = "..";
		up.className = "media-browser-item rounded-box odd-item";
		up_txt.className = "media-browser-item-text";
		up.appendChild(up_txt);
		up.addEventListener("click", function() {
			_thiz.ShowArtistsList();
		});
		list.appendChild(up);

		for (var i = 0; i < albums.length; i++) {
			var item = document.createElement("div");
			var text = document.createElement("div");
			var add = document.createElement("i");
			var txt = document.createTextNode(albums[i]);

			if (i % 2) {
				item.className = "media-browser-item rounded-box odd-item";
			} else {
				item.className = "media-browser-item rounded-box even-item";
			}
			text.className = "space-for-add media-browser-item-text";
			add.className = "icon-plus button media-add";

			text.appendChild(txt);
			item.appendChild(text);
			item.appendChild(add);

			item.addEventListener("click", function() {
				_thiz.SelectAlbum(_selectedArtist, this.textContent);
			});

			var _bcore = _core;
			add.addEventListener("click", function(e) {
				e.stopPropagation();
				var album = this.parentNode.textContent;
				_bcore.GetSongs(artist, album, function(songs) {
					_thiz.AddSongsToPlaylist(artist, album, songs);
				});
			});
			list.appendChild(item);
		}
	});
}

BasileusUI.prototype.SelectAlbum = function(artist, album)
{
	_core.GetSongs(artist, album, function(songs) {
		var list = document.getElementById("media-browser");
		var hdr = document.getElementById("media-browser-header");

		hdr.textContent = album;

		while (list.lastChild) {
			list.removeChild(list.lastChild);
		}

		var up = document.createElement("div");
		var up_txt = document.createElement("div");
		up_txt.textContent = "..";
		up.className = "media-browser-item rounded-box odd-item";
		up_txt.className = "media-browser-item-text";
		up.appendChild(up_txt);
		up.addEventListener("click", function() {
			_thiz.SelectArtist(artist);
		});
		list.appendChild(up);

		for (var i = 0; i < songs.length; i++) {
			var item = document.createElement("div");
			var text = document.createElement("div");
			var add = document.createElement("i");
			var txt = document.createTextNode(songs[i][0]);

			if (i % 2) {
				item.className = "media-browser-item rounded-box odd-item";
			} else {
				item.className = "media-browser-item rounded-box even-item";
			}
			text.className = "space-for-add media-browser-item-text";
			add.className = "icon-plus button media-add";

			text.appendChild(txt);
			item.appendChild(text);
			item.appendChild(add);

			item.setAttribute('song-id', songs[i][2]);
			item.setAttribute('song-length', songs[i][1]);

			var queueSong = function(songNode) {
				var title = songNode.textContent;
				var length = songNode.getAttribute('song-length');
				var id = songNode.getAttribute('song-id');
				var list = [ [ title, length, id ] ];
				_thiz.AddSongsToPlaylist(_selectedArtist, album, list);
			};

			item.addEventListener("click", function() {
				queueSong(this);
			});
			add.addEventListener("click", function(e) {
				e.stopPropagation();
				queueSong(this.parentNode);
			});
			list.appendChild(item);
		}
	});
}

BasileusUI.prototype.AddSongsToPlaylist = function(artist, album, songs)
{
	var playlist = document.getElementById("playlist-items");
	var _thiz = this;

	for (var i=0; i < songs.length; i++) {
		var row = document.createElement('div');

		var txt_wrapper = document.createElement('div');
		var track_cell = document.createElement('div');
		var title_cell = document.createElement('div');
		var album_cell = document.createElement('div');
		var artist_cell = document.createElement('div');
		var length_cell = document.createElement('div');

		var button_wrapper = document.createElement('div');
		var del_song = document.createElement('i');

		var track = _playlist.length + i + 1;

		if (track % 2) {
			row.className = "playlist-row rounded-box odd-item"
		} else {
			row.className = "playlist-row rounded-box even-item"
		}
		txt_wrapper.className = "playlist-text-wrapper";
		track_cell.className = "playlist-track-col";
		title_cell.className = "playlist-title-col padded-text-box";
		album_cell.className = "playlist-album-col padded-text-box";
		artist_cell.className = "playlist-artist-col padded-text-box";
		length_cell.className = "playlist-length-col";

		button_wrapper.className = "playlist-button-wrapper";
		del_song.className = "button remove-song icon-remove"

		track_cell.textContent = track;
		title_cell.textContent = songs[i][0];
		album_cell.textContent = album;
		artist_cell.textContent = artist;
		length_cell.textContent = seconds_to_string(songs[i][1]);

		txt_wrapper.appendChild(track_cell);
		txt_wrapper.appendChild(title_cell);
		txt_wrapper.appendChild(album_cell);
		txt_wrapper.appendChild(artist_cell);
		txt_wrapper.appendChild(length_cell);
		button_wrapper.appendChild(del_song);
		row.appendChild(txt_wrapper);
		row.appendChild(button_wrapper);

		row.addEventListener('click', function(e) {
			var song = parseInt(this.children[0].children[0].textContent) - 1;
			_thiz.Play(song);
		});
		del_song.addEventListener('click', function(e) {
			e.stopPropagation();
			var index = parseInt(this.parentNode.parentNode.children[0].children[0].textContent) - 1;
			_thiz.RemoveFromPlaylist(index);
		});

		playlist.appendChild(row);

		songs[i][3] = artist;
		songs[i][4] = album;
	}
	_playlist = _playlist.concat(songs);
}

BasileusUI.prototype.ClearPlaylist = function()
{
	var playlist = document.getElementById('playlist-items');
	while (playlist.lastChild) {
		playlist.removeChild(playlist.lastChild);
	}
	_playlist = [];
	_currentSong = undefined;
}

BasileusUI.prototype.RemoveFromPlaylist = function(index)
{
	var playlist = document.getElementById('playlist-items');
	_playlist.splice(index, 1);
	playlist.removeChild(playlist.children[index]);

	for (var i = 0; i < _playlist.length; i++) {
		var row = playlist.children[i];
		row.firstChild.firstChild.textContent = i + 1;

		if (i % 2) {
			if (row.className.indexOf("odd-item")) {
				var s = row.className.replace('odd-item', 'even-item');
				row.className = s;
			}
		} else {
			if (row.className.indexOf("even-item")) {
				var s = row.className.replace('even-item', 'odd-item');
				row.className = s;
			}
		}
	}

	if (index == _currentSong) {
		if (index >= _playlist.length) {
			index = _playlist.length - 1;
		}
		this.Play(index);
	} else if (index < _currentSong) {
		_currentSong--;
	}
}

BasileusUI.prototype.Play = function(song)
{
	if (_currentSong != undefined && song == undefined && _player.paused) {
		_player.play();
		return;
	}

	if (_playlist === undefined || _playlist.length == 0) {
		return;
	}

	if (_currentSong == undefined || _playlist.length < _currentSong ) {
		_currentSong = 0;
	}

	var playlist = document.getElementById('playlist-items');
	if (playlist.children[_currentSong] !== undefined) {
		var s = playlist.children[_currentSong].className;
		s = s.replace(" selected-item", "");
		playlist.children[_currentSong].className = s;
	}

	if (song !== undefined) {
		_currentSong = song;
	}

	playlist.children[_currentSong].className += " selected-item";

	var songID = _playlist[_currentSong][2];
	if (_play_timer) {
		window.clearTimeout(_play_timer);
		_play_timer = null;
	}
	_play_timer = window.setTimeout(function() {
		_player.src = _core.GetSongURI(songID);
		_player.load();
		_player.play();
	}, PLAY_TIMEOUT);
}

BasileusUI.prototype.UpdatePlayPauseButton = function ()
{
	var pp = document.getElementById("play-pause-button");
	var _thiz = this;

	if (_player.paused) {
		pp.className = "button control-panel-button icon-play";
		pp.onclick = function() {
			_thiz.Play();
		};
	} else {
		pp.className = "button control-panel-button icon-pause";
		pp.onclick = function() {
			_thiz.Pause();
		};
	}
}

BasileusUI.prototype.UpdateNowPlaying = function()
{
	var d = document.getElementById('now-playing');
	var song = _playlist[_currentSong];
	d.textContent = song[0] + ' / ' + song[3];
}

BasileusUI.prototype.UpdateProgressBar = function()
{
	var bar = document.getElementById('progress-bar');
	var time = document.getElementById('progress-time');
	var pad = bar.getBoundingClientRect().left - bar.parentNode.getBoundingClientRect().left;
	var barMaxWidth = bar.parentNode.offsetWidth - (2 * pad);
	var pw = Math.floor(barMaxWidth * _player.currentTime / _player.duration);
	if (isNaN(pw)) {
			pw = 0;
	} else if (pw > barMaxWidth) {
		pw = barMaxWidth;
	}
	bar.style.width = pw + "px";
	time.textContent = seconds_to_string(Math.round(_player.currentTime));
}

BasileusUI.prototype.UpdateBufferedBar = function()
{
	var bar = document.getElementById('buffered-bar');
	if (_player.buffered.length > 0) {
		var pad = bar.getBoundingClientRect().left - bar.parentNode.getBoundingClientRect().left;
		var barMaxWidth = bar.parentNode.offsetWidth - (2 * pad);
		var bw = Math.round(barMaxWidth * (_player.buffered.end(0) / _player.duration));
		if (bw > barMaxWidth) {
			bw = barMaxWidth;
		}
		bar.style.width = bw + "px";
	} else {
		bar.style.width = "0px";
	}
}

BasileusUI.prototype.PreviewPosition = function(posX)
{
	if (_player.seekable.length > 0 && _player.seekable.end(0) > 0) {
		var sp = document.getElementById('seek-preview');
		var bar = document.getElementById('progress-bar');
		var pad = bar.getBoundingClientRect().left - sp.parentNode.getBoundingClientRect().left;
		var boxWidth = sp.offsetWidth;
		var barMaxWidth = sp.parentNode.offsetWidth - (2 * pad);

		var pos = (posX - sp.parentNode.getBoundingClientRect().left - pad);
		var time = Math.round(pos / barMaxWidth * _player.duration);
		if (time < 0) {
			time = 0;
		}

		if (pos > barMaxWidth - boxWidth + pad) {
			pos = barMaxWidth - boxWidth + pad;
		} else if (pos < pad) {
			pos = pad;
		}
		
		sp.style.left = pos + "px";
		sp.textContent = seconds_to_string(time);
		return true;
	} else {
		return false;
	}
}

BasileusUI.prototype.Pause = function()
{
	_player.pause();
}

BasileusUI.prototype.Next = function(auto)
{
	var song = 0;

	if (_playlist.length == 1 && !_player.paused) {
		return;
	}

	if (_random && _playlist.length > 1) {
		/* Avoid playing the current song twice in a row */
		do  {
			song = Math.ceil(Math.random() * _playlist.length) - 1;
		} while (song == _currentSong)
	} else {
		if (_currentSong != undefined) {
			song = _currentSong + 1;
		}
		if (song >= _playlist.length) {
			song = 0;
			if (!_repeat && auto) {
				return;
			}
		}
	}
	this.Play(song);
}

BasileusUI.prototype.Prev = function()
{
	var song = _playlist.length - 1;

	if (_playlist.length == 1 && !_player.paused) {
		return;
	}

	if (_random && _playlist.length > 1) {
		/* Avoid playing the current song twice in a row */
		do  {
			song = Math.ceil(Math.random() * _playlist.length) - 1;
		} while (song == _currentSong)
	} else {
		if (_currentSong != undefined) {
			song = _currentSong - 1;
		}
		if (song < 0) {
			song = _playlist.length - 1;
		}
	}
	this.Play(song);
}

BasileusUI.prototype.ToggleButton = function(id)
{
	var b = document.getElementById(id);
	var s = b.className;
	var toggle;

	if (s.indexOf('button-toggle-inactive') !== -1) {
		s =s.replace('button-toggle-inactive', 'button-toggle-active');
		toggle = true;
	} else {
		s = s.replace('button-toggle-active', 'button-toggle-inactive');
		toggle = false;
	}
	b.className = s;
	return toggle;
}

BasileusUI.prototype.ToggleRepeat = function()
{
	_repeat = this.ToggleButton('toggle-repeat');
}

BasileusUI.prototype.ToggleRandom = function()
{
	_random = this.ToggleButton('toggle-random');
}

BasileusUI.prototype.SeekTo = function(posX)
{
	var bar = document.getElementById('progress-bar');
	var pad = bar.getBoundingClientRect().left - bar.parentNode.getBoundingClientRect().left;
	var barWidth = bar.parentNode.offsetWidth - 2 * pad;
	var position = (posX - bar.parentNode.getBoundingClientRect().left - pad) / barWidth;

	if (!isNaN(_player.duration * position)) {
		_player.currentTime = position * _player.duration;
		this.UpdateProgressBar();
	}
}

BasileusUI.prototype.HandleMediaError = function(error)
{
	console.log("In HandleMediaError " + error);
	switch (error.code) {
	case error.MEDIA_ERR_ABORTED:
		console.error('MEDIA_ERR_ABORTED');
		break;
	case error.MEDIA_ERR_NETWORK:
		console.error('MEDIA_ERR_NETWORK');
		break;
	case error.MEDIA_ERR_DECODE:
		console.error('MEDIA_ERR_DECODE');
		break;
	case error.MEDIA_ERR_SRC_NOT_SUPPORTED:
		console.error('MEDIA_ERR_SRC_NOT_SUPPORTED');
		break;
	 default:
		console.error('An unknown error occurred.');
		break;
	}
	_player.pause();
}

function seconds_to_string(sec)
{
	var h = Math.floor(sec / 3600);
	var m = Math.floor(sec / 60);
	var s = sec % 59;

	var str = "";
	if (h > 0)
		str += h + ":";
	str += m + ":";
	if (s < 10)
		str += "0" + s;
	else
		str += "" + s;

	return str;
}

var UI = new BasileusUI();
