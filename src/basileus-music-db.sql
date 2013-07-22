PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS artists(
	id		INTEGER PRIMARY KEY,
	name		TEXT,
	UNIQUE(name) ON CONFLICT IGNORE
);

CREATE TABLE IF NOT EXISTS albums(
	id		INTEGER PRIMARY KEY,
	name		TEXT,
	artist_id	INTEGER,
	FOREIGN KEY(artist_id)	REFERENCES artists(id),
	CONSTRAINT unq UNIQUE(name, artist_id) ON CONFLICT IGNORE
);

CREATE TABLE IF NOT EXISTS songs(
	id 		INTEGER PRIMARY KEY,
	title		TEXT,
	path		TEXT,
	hash		TEXT,
	track		INTEGER,
	length		INTEGER,
	artist_id	INTEGER,
	album_id	INTEGER,
	FOREIGN KEY(artist_id)	REFERENCES artists(id),
	FOREIGN KEY(album_id)	REFERENCES albums(id),
	UNIQUE(path) ON CONFLICT IGNORE,
	UNIQUE(hash) ON CONFLICT IGNORE
);
