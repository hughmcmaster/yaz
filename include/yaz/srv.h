/* This file is part of the YAZ toolkit.
 * Copyright (C) 1995-2009 Index Data.
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Index Data nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * \brief socket manager
 */
#ifndef YAZ_SRV_H
#define YAZ_SRV_H

#include <stddef.h>
#include <yaz/poll.h>
#include <yaz/zgdu.h>

YAZ_BEGIN_CDECL

typedef struct yaz_srv_s *yaz_srv_t;
typedef struct yaz_pkg_s *yaz_pkg_t;

struct cs_session;

typedef void (yaz_srv_gdu_handler_t)(yaz_pkg_t pkg, void *user);
typedef void *(yaz_srv_session_handler_t)(struct cs_session *cs);

YAZ_EXPORT
yaz_srv_t yaz_srv_create(const char **listeners_str);

YAZ_EXPORT
void yaz_srv_destroy(yaz_srv_t p);

YAZ_EXPORT
void yaz_srv_run(yaz_srv_t p, yaz_srv_session_handler_t *session_handler,
                 yaz_srv_gdu_handler_t *gdu_handler);

YAZ_EXPORT
void yaz_pkg_destroy(yaz_pkg_t pkg);

YAZ_EXPORT
Z_GDU **yaz_pkg_get_gdu(yaz_pkg_t pkg);

YAZ_EXPORT
ODR yaz_pkg_get_odr(yaz_pkg_t pkg);

YAZ_EXPORT
void yaz_pkg_stop_server(yaz_pkg_t pkg);

YAZ_EXPORT
void yaz_pkg_close(yaz_pkg_t pkg);

YAZ_EXPORT
yaz_pkg_t yaz_pkg_create(yaz_pkg_t request_pkg);

YAZ_EXPORT
Z_GDU *zget_wrap_APDU(ODR o, Z_APDU *apdu);

YAZ_EXPORT
void yaz_pkg_send(yaz_pkg_t pkg);

YAZ_END_CDECL

#endif
/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

