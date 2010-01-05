
#include <yaz/sock_man.h>
#include <yaz/log.h>
#include <yaz/nmem.h>
#include <assert.h>
#include <sys/epoll.h>
#include <unistd.h>

struct yaz_sock_man_s {
    yaz_sock_chan_t chan_list;
    yaz_sock_chan_t free_list;
    yaz_sock_chan_t timeout_list;
    NMEM nmem;
    int epoll_handle;
    int maxevents;
    int event_no;
    int event_ret;
    int timeout;
    int rescan;
    struct epoll_event *events;
};

struct yaz_sock_chan_s {
    yaz_sock_chan_t next;
    yaz_sock_chan_t prev;
    int fd;
    unsigned input_mask;
    unsigned output_mask;
    int max_idle;
    void *data;
    yaz_sock_man_t man;
};

yaz_sock_man_t yaz_sock_man_new(void)
{
    NMEM nmem = nmem_create();
    yaz_sock_man_t man = nmem_malloc(nmem, sizeof(*man));
    man->nmem = nmem;
    man->chan_list = 0;
    man->free_list = 0;
    man->timeout_list = 0;
    man->epoll_handle = epoll_create(100);
    man->maxevents = 30;
    man->event_no = 0;
    man->event_ret = 0;
    man->timeout = -1;
    man->rescan = 0;
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
        while (man->chan_list)
        {
            yaz_log(YLOG_WARN, "yaz_sock_man_destroy: closing %p",
                    man->chan_list);
            yaz_sock_chan_destroy(man->chan_list);
        }
        if (man->epoll_handle != -1)
            close(man->epoll_handle);
        nmem_destroy(man->nmem);
    }
}

static void poll_ctl(int op, yaz_sock_chan_t p)
{
    struct epoll_event event;

    event.events = 0;
    if (p->input_mask & yaz_poll_read)
        event.events |= EPOLLIN;
    if (p->input_mask & yaz_poll_write)
        event.events |= EPOLLOUT;
    if (p->input_mask & yaz_poll_except)
        event.events |= EPOLLERR;

    event.data.ptr = p;
    epoll_ctl(p->man->epoll_handle, op, p->fd, &event);

}

yaz_sock_chan_t yaz_sock_chan_new(yaz_sock_man_t srv, int fd, void *data,
                                  unsigned mask)
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
    p->input_mask = mask;
    p->output_mask = 0;
    p->data = data;
    p->max_idle = -1;
    p->man = srv;

    poll_ctl(EPOLL_CTL_ADD, p);
    return p;
}

static void rescan_timeout(yaz_sock_man_t man)
{
    if (man->rescan)
    {
        int timeout = -1;
        yaz_sock_chan_t p;
        for (p = man->chan_list; p; p = p->next)
            if (p->max_idle != -1 && (timeout == -1 || p->max_idle < timeout))
                timeout = p->max_idle;
        man->timeout = timeout;
        man->rescan = 0;
    }
}

void yaz_sock_chan_destroy(yaz_sock_chan_t p)
{
    yaz_sock_man_t srv = p->man;
    if (p->prev)
        p->prev->next = p->next;
    else
    {
        assert(srv->chan_list == p);
        srv->chan_list = p->next;
    }

    if (p->next)
        p->next->prev = p->prev;

    poll_ctl(EPOLL_CTL_DEL, p);

    p->next = srv->free_list;
    p->prev = 0;
    srv->free_list = p;

    srv->rescan = 1;
}

yaz_sock_chan_t yaz_sock_man_wait(yaz_sock_man_t man)
{
    struct epoll_event *ev;
    yaz_sock_chan_t chan = 0;

    if (man->timeout_list)
    { /* possibly timeout events not returned */
        for (chan = man->timeout_list; chan; chan = chan->next)
            if (chan->max_idle == man->timeout)
                break;
        if (chan)
        {
            man->timeout_list = chan->next;
            chan->output_mask = yaz_poll_timeout;
            return chan;
        }
        man->timeout_list = 0; /* no more timeout events */
    }
    assert(man->timeout_list == 0);
    assert(man->event_no <= man->event_ret);
    if (man->event_no == man->event_ret)
    { /* must wait again */
        rescan_timeout(man);
        man->event_no = 0;
        man->event_ret = epoll_wait(man->epoll_handle, man->events,
                                    man->maxevents, man->timeout);
        if (man->event_ret == 0)
        {
            /* timeout */
            for (chan = man->chan_list; chan; chan = chan->next)
                if (chan->max_idle == man->timeout)
                    break;
            assert(chan); /* there must be one chan in a timeout state */
            man->timeout_list = chan->next;
            chan->output_mask = yaz_poll_timeout;
            return chan;
        }
        else if (man->event_ret < 0)
        {
            /* error */
            return 0;
        }
    }
    ev = man->events + man->event_no;
    chan = ev->data.ptr;
    chan->output_mask = 0;
    if (ev->events & EPOLLIN)
        chan->output_mask |= yaz_poll_read;
    if (ev->events & EPOLLOUT)
        chan->output_mask |= yaz_poll_write;
    if (ev->events & EPOLLERR)
        chan->output_mask |= yaz_poll_except;
    man->event_no++;
    return chan;
}

void yaz_sock_chan_set_mask(yaz_sock_chan_t chan, unsigned mask)
{
    if (chan->input_mask != mask)
    {
        chan->input_mask = mask;
        poll_ctl(EPOLL_CTL_MOD, chan);
    }
}

void yaz_sock_chan_set_max_idle(yaz_sock_chan_t chan, int max_idle)
{
    if (chan->max_idle != max_idle)
    {
        chan->max_idle = max_idle;
        chan->man->rescan = 1;
    }
}

unsigned yaz_sock_get_mask(yaz_sock_chan_t chan)
{
    return chan->output_mask;
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

