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

OPTION (USE_TAGLIB "Use TagLib metadata parsing library" ON)

IF (CMAKE_BUILD_TYPE EQUAL "DEBUG")
	OPTION (SQLITE3_PROFILE "Enable profiling of SQLITE3 statement execution" OFF)
ENDIF (CMAKE_BUILD_TYPE EQUAL "DEBUG")

INCLUDE (FindPkgConfig)
PKG_CHECK_MODULES (LIBCONFUSE REQUIRED libconfuse)
PKG_CHECK_MODULES (SQLITE3 REQUIRED sqlite3)
PKG_CHECK_MODULES (JSON_C REQUIRED json-c)
IF (USE_TAGLIB)
	PKG_CHECK_MODULES (TAGLIB_C REQUIRED taglib_c)
ELSE (USE_TAGLIB)
	PKG_CHECK_MODULES (LIBAVFORMAT REQUIRED libavformat)
ENDIF (USE_TAGLIB)

IF (CMAKE_COMPILER_IS_GNUCXX)
	SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -Wall")
	SET (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wall -ggdb -g3 -D_DEBUG")
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

ADD_SUBDIRECTORY(src)
