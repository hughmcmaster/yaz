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
#include <yaz/tpool.h>
#include <assert.h>
#include <yaz/srv.h>


enum cs_ses_type {
    cs_ses_type_listener,
    cs_ses_type_accepting,
    cs_ses_type_normal
};

struct cs_session {
    enum cs_ses_type type;
    COMSTACK cs;
    yaz_sock_chan_t chan;
    unsigned cs_put_mask;
    unsigned cs_get_mask;
    char *input_buffer;
    int input_len;
    void *user;
};
    
struct yaz_pkg_s {
    Z_GDU *gdu;
    ODR odr;
    struct cs_session *ses;
    yaz_srv_t srv;
};

struct yaz_srv_s {
    struct cs_session *listeners;
    size_t num_listeners;
    NMEM nmem;
    yaz_sock_man_t sock_man;
    yaz_tpool_t tpool;
    int stop_flag;
    yaz_srv_session_handler_t *session_handler;
    yaz_srv_gdu_handler_t *gdu_handler;
};

static void cs_session_init(struct cs_session *ses, enum cs_ses_type type)
{
    ses->type = type;
    ses->cs = 0;
    ses->chan = 0;
    ses->cs_put_mask = 0;
    ses->cs_get_mask = yaz_poll_read;
    ses->input_buffer = 0;
    ses->input_len = 0;
}

static void cs_session_destroy(struct cs_session *ses)
{
    xfree(ses->input_buffer);
    if (ses->chan)
        yaz_sock_chan_destroy(ses->chan);
    if (ses->cs)
        cs_close(ses->cs);
}

void yaz_srv_destroy(yaz_srv_t p)
{
    if (p)
    {
        size_t i;

        yaz_tpool_destroy(p->tpool);
        for (i = 0; i < p->num_listeners; i++)
        {
            cs_session_destroy(p->listeners + i);
        }
        yaz_sock_man_destroy(p->sock_man);
        nmem_destroy(p->nmem);
    }
}

yaz_srv_t yaz_srv_create(const char **listeners_str)
{
    NMEM nmem = nmem_create();
    yaz_srv_t p = nmem_malloc(nmem, sizeof(*p));
    size_t i;
    for (i = 0; listeners_str[i]; i++)
        ;
    p->nmem = nmem;

    p->stop_flag = 0;
    p->session_handler = 0;
    p->gdu_handler = 0;
    p->num_listeners = i;
    p->listeners = 
        nmem_malloc(nmem, p->num_listeners * sizeof(*p->listeners));
    p->sock_man = yaz_sock_man_new();
    p->tpool = 0;
    for (i = 0; i < p->num_listeners; i++)
    {
        void *ap;
        const char *where = listeners_str[i];
        COMSTACK l = cs_create_host(where, CS_FLAGS_NUMERICHOST, &ap);

        cs_session_init(p->listeners +i, cs_ses_type_listener);
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
            {
                p->listeners[i].cs = l; /* success */
                p->listeners[i].chan =
                    yaz_sock_chan_new(p->sock_man,
                                      cs_fileno(l),
                                      p->listeners + i,
                                      yaz_poll_read | yaz_poll_except);
            }
        }
    }

    /* check if all are OK */
    for (i = 0; i < p->num_listeners; i++)
        if (!p->listeners[i].cs)
        {
            yaz_srv_destroy(p);
            return 0;
        }
    return p;
}

static void new_session(yaz_srv_t p, COMSTACK new_line)
{
    struct cs_session *ses = xmalloc(sizeof(*ses));
    unsigned mask =  
        ((new_line->io_pending & CS_WANT_WRITE) ? yaz_poll_write : 0) |
        ((new_line->io_pending & CS_WANT_READ) ? yaz_poll_read : 0);

    if (mask)
    {
        yaz_log(YLOG_LOG, "type accepting");
        cs_session_init(ses, cs_ses_type_accepting);
    }
    else
    {
        yaz_log(YLOG_LOG, "type normal");
        cs_session_init(ses, cs_ses_type_normal);
        mask = yaz_poll_read;
        ses->user = p->session_handler(ses);
    }
    ses->cs = new_line;
    ses->chan = yaz_sock_chan_new(p->sock_man, cs_fileno(new_line), ses, mask);
}

void yaz_pkg_destroy(yaz_pkg_t pkg)
{
    if (pkg)
    {
        odr_destroy(pkg->odr);
        xfree(pkg);
    }
}

void work_handler(void *data)
{
    yaz_pkg_t pkg = (yaz_pkg_t) data;

    pkg->srv->gdu_handler(pkg, pkg->ses->user);
    yaz_pkg_destroy(pkg);
}

void work_destroy(void *data)
{
    yaz_pkg_t pkg = (yaz_pkg_t) data;
    yaz_pkg_destroy(pkg);
}


void yaz_srv_run(yaz_srv_t p, yaz_srv_session_handler_t session_handler,
                 yaz_srv_gdu_handler_t gdu_handler)
{
    yaz_sock_chan_t chan;

    p->session_handler = session_handler;
    p->gdu_handler = gdu_handler;

    assert(!p->tpool);
    p->tpool = yaz_tpool_create(work_handler, work_destroy, 20);
    while ((chan = yaz_sock_man_wait(p->sock_man)))
    {
        unsigned output_mask = yaz_sock_get_mask(chan);
        struct cs_session *ses = yaz_sock_chan_get_data(chan);

        if (p->stop_flag)
            break;
        switch (ses->type)
        {
        case cs_ses_type_listener:
            if (yaz_sock_get_mask(chan) & yaz_poll_read)
            {
                int ret = cs_listen(ses->cs, 0, 0);
                if (ret < 0)
                {
                    yaz_log(YLOG_WARN|YLOG_ERRNO, "listen failed");
                }
                else if (ret == 1)
                {
                    yaz_log(YLOG_WARN, "cs_listen incomplete");
                }
                else
                {
                    COMSTACK new_line = cs_accept(ses->cs);
                    if (new_line)
                    {
                        yaz_log(YLOG_LOG, "new session");
                        new_session(p, new_line);
                    }
                    else
                    {
                        yaz_log(YLOG_WARN|YLOG_ERRNO, "accept failed");
                    }
                }
            }
            break;
        case cs_ses_type_accepting:
            if (!cs_accept(ses->cs))
            {
                yaz_log(YLOG_WARN|YLOG_ERRNO, "cs_accept failed");
                cs_session_destroy(ses);
                xfree(ses);
            }
            else
            {
                unsigned mask =  
                    ((ses->cs->io_pending & CS_WANT_WRITE) ? yaz_poll_write : 0) |
                    ((ses->cs->io_pending & CS_WANT_READ) ? yaz_poll_read : 0);
                if (mask)
                {
                    ses->type = cs_ses_type_accepting;
                }
                else
                {
                    ses->type = cs_ses_type_normal;
                    mask = yaz_poll_read;
                }
                yaz_sock_chan_set_mask(ses->chan, mask);
            }
            break;
        case cs_ses_type_normal:
            if ((ses->cs_put_mask & yaz_poll_read) == 0 &&
                output_mask & ses->cs_get_mask)
            {
                /* receiving package */
                unsigned new_mask = yaz_poll_read;
                yaz_log(YLOG_LOG, "Receive");
                do
                {
                    int res = cs_get(ses->cs, &ses->input_buffer, &ses->input_len);
                    if (res <= 0)
                    {
                        yaz_log(YLOG_WARN, "Connection closed by client");
                        cs_session_destroy(ses);
                        xfree(ses);
                        ses = 0;
                        break;
                    }
                    else if (res == 1)
                    {
                        if (ses->cs->io_pending & CS_WANT_WRITE)
                            new_mask |= yaz_poll_write;
                        break;
                    }
                    else
                    {  /* complete package */
                        yaz_pkg_t pkg = xmalloc(sizeof(*pkg));
                        yaz_log(YLOG_LOG, "COMPLETE PACKAGE");

                        pkg->ses = ses;
                        pkg->srv = p;
                        pkg->odr = odr_createmem(ODR_DECODE);
                        odr_setbuf(pkg->odr, ses->input_buffer, res, 0);
                        if (!z_GDU(pkg->odr, &pkg->gdu, 0, 0))
                        {
                            yaz_log(YLOG_WARN, "decoding failed");
                            odr_destroy(pkg->odr);
                            xfree(pkg);
                        }
                        else
                        {
                            yaz_tpool_add(p->tpool, pkg);
                        }
                    }
                } while (cs_more(ses->cs));
                yaz_sock_chan_set_mask(chan, new_mask);
            }
            if (ses && (output_mask & ses->cs_put_mask))
            {  /* sending package */
                yaz_log(YLOG_LOG, "Sending");
            }
        }
    }
}

Z_GDU **yaz_pkg_get_gdu(yaz_pkg_t pkg)
{
    return &pkg->gdu;
}

ODR yaz_pkg_get_odr(yaz_pkg_t pkg)
{
    return pkg->odr;
}

void yaz_pkg_close(yaz_pkg_t pkg)
{
    struct cs_session *ses = pkg->ses;
    if (ses)
    {
        cs_session_destroy(ses);
        xfree(ses);
    }
    pkg->ses = 0;
}

void yaz_pkg_stop_server(yaz_pkg_t pkg)
{
    pkg->srv->stop_flag = 1;
}

yaz_pkg_t yaz_pkg_create(yaz_pkg_t request_pkg)
{
    yaz_pkg_t pkg = xmalloc(sizeof(*pkg));

    pkg->gdu = 0;
    pkg->odr = odr_createmem(ODR_ENCODE);
    pkg->ses = request_pkg->ses;
    pkg->srv = request_pkg->srv;
    return pkg;
}

Z_GDU *zget_wrap_APDU(ODR o, Z_APDU *apdu)
{
    Z_GDU *gdu = odr_malloc(o, sizeof(*gdu));
    gdu->which = Z_GDU_Z3950;
    gdu->u.z3950 = apdu;
    return gdu;
}

void yaz_pkg_send(yaz_pkg_t pkg)
{
    yaz_log(YLOG_WARN, "send.. UNFINISHED");
}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

