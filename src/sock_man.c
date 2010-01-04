
#include <yaz/sock_man.h>
#include <yaz/nmem.h>
#include <assert.h>
#include <sys/epoll.h>

struct yaz_sock_man_s {
    yaz_sock_chan_t chan_list;
    yaz_sock_chan_t free_list;
    NMEM nmem;
    int epoll_handle;
    int maxevents;
    yaz_sock_chan_t *events;
};

struct yaz_sock_chan_s {
    yaz_sock_chan_t next;
    yaz_sock_chan_t prev;
    int fd;
    unsigned mask;
    void *data;
};

yaz_sock_man_t yaz_sock_man_new(void)
{
    NMEM nmem = nmem_create();
    yaz_sock_man_t man = nmem_malloc(nmem, sizeof(*man));
    man->nmem = nmem;
    man->chan_list = 0;
    man->free_list = 0;
    man->epoll_handle = epoll_create(100);
    man->maxevents = 30;
    man->events = nmem_malloc(nmem, man->maxevents * sizeof(*man->events));
    if (man->epoll_handle == -1)
    {
        yaz_sock_man_destroy(man);
        return 0;
    }
    return man;
}

void yaz_sock_man_destroy(yaz_sock_man_t man)
{
    if (man)
    {
        if (man->epoll_handle != -1)
            close(man->epoll_handle);
        assert(man->chan_list == 0);
        nmem_destroy(man->nmem);
    }
}

yaz_sock_chan_t yaz_sock_chan_new(yaz_sock_man_t srv, int fd, void *data)
{
    yaz_sock_chan_t p;
    if (srv->free_list)
    {
        p = srv->free_list;
        srv->free_list = p->next;
    }
    else
        p = nmem_malloc(srv->nmem, sizeof(*p));
    p->next = srv->chan_list;
    if (p->next)
        p->next->prev = p;
    p->prev = 0;
    srv->chan_list = p;

    p->fd = fd;
    p->mask = 0;
    p->data = data;
    return p;
}

void yaz_sock_chan_destroy(yaz_sock_man_t srv, yaz_sock_chan_t p)
{
    if (p->prev)
        p->prev->next = p->next;
    else
    {
        assert(srv->chan_list == p);
        srv->chan_list = p->next;
    }

    if (p->next)
        p->next->prev = p->prev;
    
    p->next = srv->free_list;
    p->prev = 0;
    srv->free_list = p;
}

yaz_sock_chan_t yaz_sock_man_wait(yaz_sock_man_t man)
{
    return 0;
}

void yaz_sock_chan_set_mask(yaz_sock_chan_t chan, unsigned mask)
{
    chan->mask = mask;
}

unsigned yaz_sock_get_mask(yaz_sock_chan_t chan)
{
    return chan->mask;
}

void *yaz_sock_chan_get_data(yaz_sock_chan_t chan)
{
    return chan->data;
}


/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

