/* This file is part of the YAZ toolkit.
 * Copyright (C) 1995-2009 Index Data
 * See the file LICENSE for details.
 */
/**
 * \file 
 * \brief Small HTTP server
 */

#include <yaz/zgdu.h>
#include <yaz/comstack.h>
#include <yaz/nmem.h>
#include <yaz/log.h>
#include <yaz/sock_man.h>
#include <assert.h>

typedef struct yaz_nano_srv_s *yaz_nano_srv_t;
typedef struct yaz_nano_pkg_s *yaz_nano_pkg_t;

struct yaz_nano_pkg_s {
    void *handle;
    int listener_id;
    ODR encode_odr;
    Z_GDU *request_gdu;
    Z_GDU *response_gdu;
};

struct yaz_nano_srv_s {
    COMSTACK *cs_listeners;
    size_t num_listeners;
    NMEM nmem;
    yaz_sock_man_t sock_man;
};

void yaz_nano_srv_destroy(yaz_nano_srv_t p)
{
    if (p)
    {
        size_t i;
        for (i = 0; i < p->num_listeners; i++)
            if (p->cs_listeners[i])
                cs_close(p->cs_listeners[i]);
        yaz_sock_man_destroy(p->sock_man);
        nmem_destroy(p->nmem);
    }
}

yaz_nano_srv_t yaz_nano_srv_create(const char **listeners_str)
{
    NMEM nmem = nmem_create();
    yaz_nano_srv_t p = nmem_malloc(nmem, sizeof(*p));
    size_t i;
    for (i = 0; listeners_str[i]; i++)
        ;
    p->nmem = nmem;
    p->num_listeners = i;
    p->cs_listeners = 
        nmem_malloc(nmem, p->num_listeners * sizeof(*p->cs_listeners));
    for (i = 0; i < p->num_listeners; i++)
    {
        void *ap;
        const char *where = listeners_str[i];
        COMSTACK l = cs_create_host(where, 2, &ap);
        p->cs_listeners[i] = 0; /* not OK (yet) */
        if (!l)
        {
            yaz_log(YLOG_WARN|YLOG_ERRNO, "cs_create_host(%s) failed", where);
        }
        else
        {
            if (cs_bind(l, ap, CS_SERVER) < 0)
            {
                if (cs_errno(l) == CSYSERR)
                    yaz_log(YLOG_FATAL|YLOG_ERRNO, "Failed to bind to %s", where);
                else
                    yaz_log(YLOG_FATAL, "Failed to bind to %s: %s", where,
                            cs_strerror(l));
                cs_close(l);
            }
            else
                p->cs_listeners[i] = l; /* success */
        }
    }
    p->sock_man = yaz_sock_man_new();

    /* check if all are OK */
    for (i = 0; i < p->num_listeners; i++)
        if (!p->cs_listeners[i])
        {
            yaz_nano_srv_destroy(p);
            return 0;
        }

    for (i = 0; i < p->num_listeners; i++)
    {
        yaz_sock_chan_t chan;

        chan = yaz_sock_chan_new(p->sock_man, cs_fileno(p->cs_listeners[i]),
                                 p->cs_listeners + i,
                                 yaz_poll_read | yaz_poll_except);
    }    
    return p;
}

Z_GDU *yaz_nano_pkg_req(yaz_nano_pkg_t pkg)
{
    return pkg->request_gdu;
}

Z_GDU *yaz_nano_pkg_response(yaz_nano_pkg_t pkg)
{
    return pkg->response_gdu;
}

ODR yaz_nano_pkg_encode(yaz_nano_pkg_t pkg)
{
    return pkg->encode_odr;
}

int yaz_nano_pkg_listener_id(yaz_nano_pkg_t pkg)
{
    return pkg->listener_id;
}

yaz_nano_pkg_t yaz_nano_srv_get_pkg(yaz_nano_srv_t p)
{
    return 0;
}

void yaz_nano_srv_put_pkg(yaz_nano_srv_t p, yaz_nano_pkg_t pkg)
{

}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

