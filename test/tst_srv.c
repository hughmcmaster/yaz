/* This file is part of the YAZ toolkit.
 * Copyright (C) 1995-2009 Index Data
 * See the file LICENSE for details.
 */

#include <stdlib.h>
#include <stdio.h>

#include <yaz/log.h>
#include <yaz/test.h>
#include <yaz/srv.h>
#include <yaz/odr.h>
#include <yaz/proto.h>

struct my_info {
    int x;
};

static void *create_session(struct cs_session *ses)
{
    struct my_info *my = xmalloc(sizeof(*my));
    my->x = 42;
    yaz_log(YLOG_LOG, "create_session");
    return my;
}

static void gdu_handler(yaz_pkg_t pkg, void *user)
{
    struct my_info *my = user;
    Z_GDU **gdu = yaz_pkg_get_gdu(pkg);
    ODR o = odr_createmem(ODR_PRINT);

    yaz_log(YLOG_LOG, "gdu_handler");
    YAZ_CHECK_EQ(my->x, 42);
    
    z_GDU(o, gdu, 0, 0);
    odr_destroy(o);

    if ((*gdu)->which == Z_GDU_Z3950)
    {
        ODR encode = odr_createmem(ODR_ENCODE);
        Z_APDU *apdu_req = (*gdu)->u.z3950;
        Z_APDU *apdu_res = 0;
        int must_close = 0;

        if (apdu_req->which == Z_APDU_close)
        {
            apdu_res = zget_APDU(encode, Z_APDU_close);
            *apdu_res->u.close->closeReason = Z_Close_finished;
            must_close = 1;
        }
        else if (apdu_req->which == Z_APDU_initRequest)
        {
            apdu_res = zget_APDU(encode, Z_APDU_initResponse);
        }
        else
        {
            apdu_res = zget_APDU(encode, Z_APDU_close);

            *apdu_res->u.close->closeReason = Z_Close_unspecified;
            must_close = 1;
        }
        if (apdu_res)
        {
            yaz_pkg_t pkg_res = yaz_pkg_create(pkg);
            *yaz_pkg_get_gdu(pkg) = zget_wrap_APDU(encode, apdu_res);
            yaz_pkg_send(pkg_res);
        }
        if (must_close)
            yaz_pkg_close(pkg);
        yaz_pkg_stop_server(pkg);
    }
}

static void tst_srv(void)
{
    const char *listeners[] = {"unix:socket", 0};

    yaz_srv_t srv = yaz_srv_create(listeners);
    YAZ_CHECK(srv);
    if (!srv)
        return;

    yaz_srv_run(srv, create_session, gdu_handler);
    yaz_srv_destroy(srv);
}

int main (int argc, char **argv)
{
    YAZ_CHECK_INIT(argc, argv);
    YAZ_CHECK_LOG();
    /* tst_srv(); */
    YAZ_CHECK_TERM;
}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

