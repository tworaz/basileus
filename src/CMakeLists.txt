# Copyright (c) 2013 Peter Tworek
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
# 3. Neither the name of the author nor the names of any co-contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

SET (EXEC_NAME "basileusd")

ADD_EXECUTABLE (
	${EXEC_NAME}
	main.c
	basileus.c
	basileus.h
	configuration.c
	configuration.h
	logger.c
	logger.h
	music_db.c
	music_db.h
	mongoose.c
	mongoose.h
	webserver.c
	webserver.h
	basileus-music-db.h
	)

INCLUDE_DIRECTORIES(
	${CMAKE_BINARY_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${LIBCONFUSE_INCLUDE_DIRS}
	${TAGLIB_C_INCLUDE_DIRS}
	${SQLITE3_INCLUDE_DIRS}
	${JSON_C_INCLUDE_DIRS}
	)

TARGET_LINK_LIBRARIES(
	${EXEC_NAME}
	${LIBCONFUSE_LIBRARIES}
	${TAGLIB_C_LIBRARIES}
	${SQLITE3_LIBRARIES}
	${JSON_C_LIBRARIES}
	)

ADD_DEFINITIONS(-DNO_SSL -DNO_CGI)

ADD_CUSTOM_COMMAND(
	OUTPUT basileus-music-db.h
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/gen-db-include.sh
		${CMAKE_CURRENT_SOURCE_DIR}/basileus-music-db.sql
		${CMAKE_CURRENT_BINARY_DIR}/basileus-music-db.h
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/basileus-music-db.sql
	COMMENT "Generating sqlite3 database header file ..."
)
