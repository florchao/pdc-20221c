#include <assert.h>
#include <stdlib.h>
#include "copy.h"
#include "../../../socks5/socks5utils.h"
#include "../../stm_initialize.h"
#include "../../../selector/selector.h"
#include "../../../buffer/buffer.h"
#include "../../../stadistics/stadistics.h"

static copy_st *copy_ptr(selector_key *event);

static fd_interest check_interest(fd_selector s, copy_st *state)
{
    fd_interest ret = OP_NOOP;

    if(*state->fd != -1) 
    {
        if (((state->duplex & OP_READ) && buffer_can_write(state->read_buff)) )
        {
            ret |= OP_READ;
        }
        if ((state->duplex & OP_WRITE) && buffer_can_read(state->write_buff) )
        {
            ret |= OP_WRITE;
        }
        if (SELECTOR_SUCCESS != selector_set_interest(s, *state->fd, ret))
        {
            abort();
        }
    }

    return ret;
}

static void copy_arrival(unsigned int st, selector_key * event){
    struct copy_st * state = &((Session *) (event->data))->client_header.copy;

    state->fd =  &((Session *) (event->data))->client.fd;
    state->read_buff =  &((Session *) (event->data))->input;
    state->write_buff =  &((Session *) (event->data))->output;
    state->duplex = OP_READ | OP_WRITE;
    state->other =  &((Session *) (event->data))->server_header.copy;

    state =  &((Session *) (event->data))->server_header.copy;
    state->fd =  &((Session *) (event->data))->server.fd;
    state->read_buff = &((Session *) (event->data))->output;
    state->write_buff =  &((Session *) (event->data))->input;
    state->duplex = OP_READ | OP_WRITE;
    state->other = &((Session *) (event->data))->client_header.copy;
}

static copy_st *copy_ptr(selector_key *event)
{
    copy_st *state = &((Session *) (event->data))->client_header.copy;

    if (*state->fd == event->fd)
    {
        // ok
    }
    else
    {
        state = state->other;
    }

    return state;
}

static unsigned int copy_read(selector_key * event){

    copy_st * state = copy_ptr(event);

    assert(*state->fd == event->fd);
    size_t count;
    unsigned ret = COPY;

    uint8_t *ptr = buffer_write_ptr(state->read_buff, &count);
    ssize_t n = recv(event->fd, ptr, count, 0);
    if (n <= 0)
    {
        shutdown(*state->fd, SHUT_RD);
        state->duplex &= ~OP_READ;
        if (*state->other->fd != -1)
        {
            shutdown(*state->other->fd, SHUT_WR);
            state->other->duplex &= ~OP_WRITE;
        }
    }
    else
    {
        buffer_write_adv(state->read_buff, n);

    }
    check_interest(event->s, state);
    check_interest(event->s, state->other);
    if (state->duplex == OP_NOOP)
    {
        ret = DONE;
    }

    return ret;
}

static unsigned int copy_write(selector_key * event){
    copy_st *state = copy_ptr(event);

    assert(*state->fd == event->fd);
    size_t count;
    unsigned ret = COPY;

    uint8_t *ptr = buffer_read_ptr(state->write_buff, &count);
    ssize_t n = send(event->fd, ptr, count, MSG_NOSIGNAL);
    if (n == -1)
    {
        shutdown(*state->fd, SHUT_WR);
        state->duplex &= ~OP_WRITE;
        if (*state->other->fd != -1)
        {
            shutdown(*state->other->fd, SHUT_RD);
            state->other->duplex &= ~OP_READ;
        }
    }
    else
    {
        stadistics_increase_bytes_sent(n);
        buffer_read_adv(state->write_buff, n);
    }
    check_interest(event->s, state);
    check_interest(event->s, state->other);
    if (state->duplex == OP_NOOP)
    {
        ret = DONE;
    }

    return ret;
}

state_definition copy_state_def(void) {

    state_definition state_def = {
        .state = COPY,
        .on_arrival = copy_arrival,
        .on_read_ready = copy_read,
        .on_write_ready = copy_write,
        .on_block_ready = NULL,
        .on_departure = NULL,
    };

    return state_def;
}