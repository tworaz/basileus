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

CMAKE_MINIMUM_REQUIRED (VERSION 2.4.4)

PROJECT (basileus)
set (basileus_VERSION_MAJOR 0)
set (basileus_VERSION_MINOR 0)

OPTION (VALGRIND "Avoid code that triggers valgrind warnings, makes debugging real problems easier" OFF)
OPTION (USE_TAGLIB "Use TagLib metadata parsing library" ON)

IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
	OPTION (SQLITE3_PROFILE "Enable profiling of SQLITE3 statement execution" OFF)
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")

SET (SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc" CACHE STRING "Main configuration directory")
SET (BINDIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE STRING "Binary installation dir")
SET (DATADIR "${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}" CACHE STRING "Data storage directory")
SET (DBDIR "${CMAKE_INSTALL_PREFIX}/var/db" CACHE STRING "Music database storage directory")
SET (WWWDIR "${DATADIR}/www" CACHE STRING "WebUI installation directory")
SET (DEFAULT_MUSIC_DIR "/media/music" CACHE STRING "Default directory in which to look for music files")

INCLUDE (FindPkgConfig)
PKG_CHECK_MODULES (SQLITE3 REQUIRED sqlite3)
PKG_CHECK_MODULES (JSON_C REQUIRED json-c)
PKG_CHECK_MODULES (LIBEVENT REQUIRED libevent)
PKG_CHECK_MODULES (LIBEVENT_PTHREADS REQUIRED libevent_pthreads)

IF (USE_TAGLIB)
	PKG_CHECK_MODULES (TAGLIB_C REQUIRED taglib_c)
ELSE (USE_TAGLIB)
	PKG_CHECK_MODULES (LIBAVFORMAT REQUIRED libavformat)
ENDIF (USE_TAGLIB)

IF (CMAKE_COMPILER_IS_GNUCXX)
	SET (COMMON_FLAGS "-Wall -ansi")
	SET (CMAKE_C_FLAGS "-O2 ${COMMON_FLAGS} ${CMAKE_C_FLAGS}")
	SET (CMAKE_C_FLAGS_DEBUG "-O0 ${COMMON_FLAGS} -ggdb ${CMAKE_C_FLAGS_DEBUG}")
ELSE (CMAKE_COMPILER_IS_GNUCXX)
	MESSAGE (FATAL_ERROR "Your compiler is not currently supported!")
ENDIF (CMAKE_COMPILER_IS_GNUCXX)

CONFIGURE_FILE (
	${CMAKE_SOURCE_DIR}/config.h.in
	${CMAKE_BINARY_DIR}/config.h
)
CONFIGURE_FILE (
	${CMAKE_SOURCE_DIR}/basileus.conf.in
	${CMAKE_BINARY_DIR}/basileus.conf
)

INSTALL (FILES ${CMAKE_CURRENT_BINARY_DIR}/basileus.conf DESTINATION ${SYSCONFDIR})
FILE (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME})
INSTALL (DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME} DESTINATION ${DBDIR})

ADD_SUBDIRECTORY (src)
ADD_SUBDIRECTORY (www)
