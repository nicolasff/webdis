/* https://datatracker.ietf.org/doc/html/rfc6455 */

#include <stdlib.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <event.h>
#include <http_parser.h>

#define DEBUG_LOGS 0

struct host_info {
    char *host;
    short port;
};

enum worker_state {
    WS_INITIAL,
    WS_SENT_HANDSHAKE,
    WS_RECEIVED_HANDSHAKE,
    WS_SENT_FRAME,
    WS_COMPLETE
};

/* worker_thread, with counter of remaining messages */
struct worker_thread {
    struct host_info *hi;
    struct event_base *base;

    int msg_target;
    int msg_received;
    int msg_sent;
    int byte_count;
    pthread_t thread;
    enum worker_state state;

    struct evbuffer *rbuffer;
    int got_header;

    struct evbuffer *wbuffer;

    int verbose;
    int fd;
    struct event ev_r;
    struct event ev_w;

    http_parser parser;
    http_parser_settings settings;
};

void
hex_dump(char *p, size_t sz) {
#if DEBUG_LOGS
    printf("hex dump of %p (%ld bytes)\n", p, sz);
    for (char *cur = p; cur < p + sz; cur += 16) {
        char letters[16] = {0};
        int limit = (cur + 16) > p + sz ? (sz % 16) : 16;
        printf("%08lx ", cur - p); /* address */
        for (int i = 0; i < limit; i++) {
            printf("%02x ", (unsigned int)(cur[i] & 0xff));
            letters[i] = isprint(cur[i]) ? cur[i] : '.';
        }
        for (int i = limit; i < 16; i++) { /* pad on last line */
            printf("   ");
        }
        printf(" %.*s\n", limit, letters);
    }
#endif
}

void
evbuffer_debug_dump(struct evbuffer *buffer) {
    size_t sz = evbuffer_get_length(buffer);
    char *data = malloc(sz);
    evbuffer_remove(buffer, data, sz);
    hex_dump(data, sz);
    evbuffer_prepend(buffer, data, sz);
    free(data);
}

static void
wait_for_possible_read(struct worker_thread *wt);
static void
wait_for_possible_write(struct worker_thread *wt);

static void
ws_enqueue_frame(struct worker_thread *wt);

void
process_message(struct worker_thread *wt, size_t sz) {

    // printf("process_message\n");
    if(wt->msg_received % 10000 == 0) {
        printf("thread %u: %8d messages left (got %9d bytes so far).\n",
            (unsigned int)wt->thread,
            wt->msg_target - wt->msg_received, wt->byte_count);
    }
    wt->byte_count += sz;

    /* decrement read count, and stop receiving when we reach zero. */
    wt->msg_received++;
    if(wt->msg_received == wt->msg_target) {
        event_base_loopexit(wt->base, NULL);
    }
}

/**
 * Called when we can write to the socket.
 */
void
websocket_can_write(int fd, short event, void *ptr) {
    int ret;
    struct worker_thread *wt = ptr;
#if DEBUG_LOGS
    printf("%s (wt=%p, fd=%d)\n", __func__, wt, fd);
#endif

    if(event != EV_WRITE) {
        return;
    }
    switch (wt->state)
    {
    case WS_INITIAL: { /* still sending initial HTTP request */
        ret = evbuffer_write(wt->wbuffer, fd);
#if DEBUG_LOGS
        printf("evbuffer_write returned %d\n", ret);
        printf("evbuffer_get_length returned %d\n", evbuffer_get_length(wt->wbuffer));
#endif
        if (evbuffer_get_length(wt->wbuffer) != 0) { /* not all written */
            wait_for_possible_write(wt);
            return;
        }
        /* otherwise, we've sent the full request, time to read the response */
        wt->state = WS_SENT_HANDSHAKE;
#if DEBUG_LOGS
        printf("state=WS_SENT_HANDSHAKE\n");
#endif
        wait_for_possible_read(wt);
        return;
    }
    case WS_RECEIVED_HANDSHAKE: { /* ready to send a frame */
#if DEBUG_LOGS
        printf("About to send data for WS frame, %lu in buffer\n", evbuffer_get_length(wt->wbuffer));
#endif
        evbuffer_write(wt->wbuffer, fd);
        size_t write_remains = evbuffer_get_length(wt->wbuffer);
#if DEBUG_LOGS
        printf("Sent data for WS frame, still %lu left to write\n", write_remains);
#endif
        if (write_remains == 0) { /* ready to read response */
            wt->state = WS_SENT_FRAME;
            wt->msg_sent++;
            wait_for_possible_read(wt);
        } else { /* not finished writing */
            wait_for_possible_write(wt);
        }
        return;
    }
    default:
        break;
    }
#if 0
    char message[] = "\x00[\"SET\",\"key\",\"value\"]\xff\x00[\"GET\",\"key\"]\xff";
    ret = write(fd, message, sizeof(message)-1);
    if(ret != sizeof(message)-1) {
        fprintf(stderr, "write on %d failed: %s\n", fd, strerror(errno));
        close(fd);
    }

    wt->msg_sent += 2;
    if(wt->msg_sent < wt->msg_target) {
        event_set(&wt->ev_w, fd, EV_WRITE, websocket_can_write, wt);
        event_base_set(wt->base, &wt->ev_w);
        ret = event_add(&wt->ev_w, NULL);
    }
#endif
}

static void
websocket_can_read(int fd, short event, void *ptr) {
    char packet[2048], *pos;
    int ret, success = 1;

    struct worker_thread *wt = ptr;
#if DEBUG_LOGS
    printf("%s (wt=%p)\n", __func__, wt);
#endif

    if(event != EV_READ) {
        return;
    }

    /* read message */
    ret = evbuffer_read(wt->rbuffer, fd, 65536);
#if DEBUG_LOGS
    printf("evbuffer_read() returned %d; wt->state=%d. wt->rbuffer:\n", ret, wt->state);
#endif
    evbuffer_debug_dump(wt->rbuffer);
    if (ret == 0) {
#if DEBUG_LOGS
        printf("We didn't read anything from the socket...\n");
#endif
        wait_for_possible_read(wt);
        return;
    }

    while(1) {
        switch (wt->state) {
        case WS_SENT_HANDSHAKE: { /* waiting for handshake response */
            size_t avail_sz = evbuffer_get_length(wt->rbuffer);
            char *tmp = calloc(avail_sz, 1);
#if DEBUG_LOGS
            printf("avail_sz from rbuffer = %lu\n", avail_sz);
#endif
            evbuffer_remove(wt->rbuffer, tmp, avail_sz); /* copy into `tmp` */
#if DEBUG_LOGS
            printf("Giving %lu bytes to http-parser\n", avail_sz);
#endif
            int nparsed = http_parser_execute(&wt->parser, &wt->settings, tmp, avail_sz);
#if DEBUG_LOGS
            printf("http-parser returned %d\n", nparsed);
#endif
            if (nparsed != (int)avail_sz) { // put back what we didn't read
#if DEBUG_LOGS
                printf("re-attach (prepend) %lu bytes\n", avail_sz - nparsed);
#endif
                evbuffer_prepend(wt->rbuffer, tmp + nparsed, avail_sz - nparsed);
            }
            free(tmp);
            if (wt->state == WS_SENT_HANDSHAKE &&  /* haven't encountered end of response yet */
                wt->parser.upgrade && nparsed != (int)avail_sz) {
#if DEBUG_LOGS
                printf("UPGRADE *and* we have some data left\n");
#endif
                continue;
            } else if (wt->state == WS_RECEIVED_HANDSHAKE) { /* we have the full response */
                evbuffer_drain(wt->rbuffer, evbuffer_get_length(wt->rbuffer));
            }
        }
        return;

        case WS_SENT_FRAME: { /* waiting for frame response */
#if DEBUG_LOGS
            printf("We're in WS_SENT_FRAME, just read a frame response. wt->rbuffer:\n");
#endif
            evbuffer_debug_dump(wt->rbuffer);
            uint8_t flag_opcodes, payload_len;
            if (evbuffer_get_length(wt->rbuffer) < 2) { /* not enough data */
                wait_for_possible_read(wt);
                return;
            }
            evbuffer_remove(wt->rbuffer, &flag_opcodes, 1); /* remove flags & opcode */
            evbuffer_remove(wt->rbuffer, &payload_len, 1);  /* remove length */
            evbuffer_drain(wt->rbuffer, (size_t)payload_len); /* remove payload itself */
            process_message(wt, payload_len);

            if (evbuffer_get_length(wt->rbuffer) == 0) { /* consumed everything, let's write again */
#if DEBUG_LOGS
                printf("our turn to write again\n");
#endif
                wt->state = WS_RECEIVED_HANDSHAKE;
                ws_enqueue_frame(wt);
                return;
            } else {
#if DEBUG_LOGS
                printf("there's still data to consume\n");
#endif
                continue;
            }
#if 0
            struct evbuffer_ptr sof = evbuffer_search(wt->rbuffer, "\x00", 1, NULL);
            struct evbuffer_ptr eof = evbuffer_search(wt->rbuffer, "\xff", 1, NULL);
            if (evbuffer_get_length(wt->rbuffer) >= 1 && sof.pos != 0) {
                printf("ERROR: length=%lu, sof at pos %ld\n", evbuffer_get_length(wt->rbuffer), sof.pos);
            }
            if (eof.pos == -1) { /* not there yet */
                printf("Couldn't find the end-of-frame marker, need to read more\n");
                wait_for_possible_read(wt);
            } else { /* we have a frame */
                size_t bounded_frame_sz = eof.pos + 1;
                char *bounded_frame = calloc(bounded_frame_sz, 1);
                evbuffer_remove(wt->rbuffer, bounded_frame, bounded_frame_sz);
                process_message(wt, bounded_frame_sz);
                printf("Received frame (%lu bytes total):\n", bounded_frame_sz);
                hex_dump(bounded_frame, bounded_frame_sz);
                free(bounded_frame);
                if (evbuffer_get_length(wt->rbuffer) > 0) { /* we may have more frames to process */
                    continue;
                } else { /* our turn to send a frame */
                    printf("Add frame to write buffer\n");
                    ws_enqueue_frame(wt);
                }
            }
#endif
        }
        return;

        default:
            return;
        }
    }

#if 0
    pos = packet;
    if(ret > 0) {
        char *data, *last;
        int sz, msg_sz;

        if(wt->got_header == 0) { /* first response */
            char *frame_start = strstr(packet, "MH"); /* end of the handshake */
            if(frame_start == NULL) {
                return; /* not yet */
            } else { /* start monitoring possible writes */
                printf("start monitoring possible writes\n");
                evbuffer_add(wt->rbuffer, frame_start + 2, ret - (frame_start + 2 - packet));

                wt->got_header = 1;
                event_set(&wt->ev_w, fd, EV_WRITE,
                        websocket_can_write, wt);
                event_base_set(wt->base, &wt->ev_w);
                ret = event_add(&wt->ev_w, NULL);
            }
        } else {
            /* we've had the header already, now bufffer data. */
            evbuffer_add(wt->rbuffer, packet, ret);
        }

        while(1) {
            data = (char*)EVBUFFER_DATA(wt->rbuffer);
            sz = EVBUFFER_LENGTH(wt->rbuffer);

            if(sz == 0) { /* no data */
                break;
            }
            if(*data != 0) { /* missing frame start */
                success = 0;
                break;
            }
            last = memchr(data, 0xff, sz); /* look for frame end */
            if(!last) {
                /* no end of frame in sight. */
                break;
            }
            msg_sz = last - data - 1;
            process_message(ptr, msg_sz); /* record packet */

            /* drain including frame delimiters (+2 bytes) */
            evbuffer_drain(wt->rbuffer, msg_sz + 2);
        }
    } else {
        printf("ret=%d\n", ret);
        success = 0;
    }
    if(success == 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        event_base_loopexit(wt->base, NULL);
    }
#endif
}


static void
wait_for_possible_read(struct worker_thread *wt) {
#if DEBUG_LOGS
    printf("%s (wt=%p)\n", __func__, wt);
#endif
    event_set(&wt->ev_r, wt->fd, EV_READ, websocket_can_read, wt);
    event_base_set(wt->base, &wt->ev_r);
    event_add(&wt->ev_r, NULL);
}

static void
wait_for_possible_write(struct worker_thread *wt) {
#if DEBUG_LOGS
    printf("%s (wt=%p)\n", __func__, wt);
#endif
    event_set(&wt->ev_r, wt->fd, EV_WRITE, websocket_can_write, wt);
    event_base_set(wt->base, &wt->ev_r);
    event_add(&wt->ev_r, NULL);
}

static int
ws_on_headers_complete(http_parser *p) {
    struct worker_thread *wt = p->data;

#if DEBUG_LOGS
    printf("%s (wt=%p)\n", __func__, wt);
#endif
    // TODO
    return 0;
}

static void
ws_enqueue_frame_for_command(struct worker_thread *wt, char *cmd, size_t sz) {
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() & 0xff;
    }
    uint8_t len = (uint8_t)(sz); /* (1 << 7) | length. */
    len |= (1 << 7); /* set masking bit ON */

    for (int i = 0; i < sz; i++) {
        cmd[i] = (cmd[i] ^ mask[i%4]) & 0xff;
    }
    /* 0x81 = 10000001b: FIN bit (only one message in the frame), text frame */
    evbuffer_add(wt->wbuffer, "\x81", 1);
    evbuffer_add(wt->wbuffer, &len, 1);
    evbuffer_add(wt->wbuffer, mask, 4);
    evbuffer_add(wt->wbuffer, cmd, sz);
}

static void
ws_enqueue_frame(struct worker_thread *wt) {

#if 0
    char set_command[] = "[\"SET\",\"key\",\"value\"]";
    ws_enqueue_frame_for_command(wt, set_command, sizeof(set_command) - 1);
    char get_command[] = "[\"GET\",\"key\"]";
    ws_enqueue_frame_for_command(wt, get_command, sizeof(get_command) - 1);
#endif
    char ping_command[] = "[\"PING\"]";
    ws_enqueue_frame_for_command(wt, ping_command, sizeof(ping_command) - 1);

    wait_for_possible_write(wt);
}

static int
ws_on_message_complete(http_parser *p) {
    struct worker_thread *wt = p->data;

#if DEBUG_LOGS
    printf("%s (wt=%p)\n", __func__, wt);
#endif
    // we've received the full HTTP response now, so we're ready to send frames
    wt->state = WS_RECEIVED_HANDSHAKE;
    ws_enqueue_frame(wt); /* add frame to buffer and register interest in writing */
    return 0;
}

void*
worker_main(void *ptr) {

    char ws_template[] = "GET /.json HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: WebSocket\r\n"
                "Origin: http://%s:%d\r\n"
                "Sec-WebSocket-Key: webdis-websocket-test-key\r\n"
                "\r\n"
                ;

    struct worker_thread *wt = ptr;

    int ret;
    int fd;
    struct sockaddr_in addr;
    char *ws_handshake;
    size_t ws_handshake_sz;

    /* connect socket */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(wt->hi->port);
    memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
    addr.sin_addr.s_addr = inet_addr(wt->hi->host);

    ret = connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
    if(ret != 0) {
        fprintf(stderr, "connect: ret=%d: %s\n", ret, strerror(errno));
        return NULL;
    }

    /* initialize worker thread */
    wt->fd = fd;
    wt->base = event_base_new();
    wt->rbuffer = evbuffer_new();
    wt->wbuffer = evbuffer_new(); /* write buffer */
    wt->byte_count = 0;
    wt->got_header = 0;

    /* initialize HTTP parser, to parse the server response */
    memset(&wt->settings, 0, sizeof(http_parser_settings));
    wt->settings.on_headers_complete = ws_on_headers_complete;
    wt->settings.on_message_complete = ws_on_message_complete;
    http_parser_init(&wt->parser, HTTP_RESPONSE);
    wt->parser.data = wt;

    /* build handshake buffer */
    /*
    ws_handshake_sz = sizeof(ws_handshake)
        + 2*strlen(wt->hi->host) + 500;
    ws_handshake = calloc(ws_handshake_sz, 1);
    ws_handshake_sz = (size_t)sprintf(ws_handshake, ws_template,
            wt->hi->host, wt->hi->port,
            wt->hi->host, wt->hi->port);
    */
    int added = evbuffer_add_printf(wt->wbuffer, ws_template, wt->hi->host, wt->hi->port,
                                    wt->hi->host, wt->hi->port);
    wait_for_possible_write(wt); /* request callback */

    /* go! */
    event_base_dispatch(wt->base);
    printf("event_base_dispatch returned\n");
    event_base_free(wt->base);
    // free(ws_handshake);
    return NULL;
}

void
usage(const char* argv0, char *host_default, short port_default,
        int thread_count_default, int messages_default) {

    printf("Usage: %s [options]\n"
        "Options are:\n"
        "\t-h host\t\t(default = \"%s\")\n"
        "\t-p port\t\t(default = %d)\n"
        "\t-c threads\t(default = %d)\n"
        "\t-n count\t(number of messages per thread, default = %d)\n"
        "\t-v\t\t(verbose)\n",
        argv0, host_default, (int)port_default,
        thread_count_default, messages_default);
}

int
main(int argc, char *argv[]) {

    struct timespec t0, t1;

    int messages_default = 100000;
    int thread_count_default = 4;
    short port_default = 7379;
    char *host_default = "127.0.0.1";

    int msg_target = messages_default;
    int thread_count = thread_count_default;
    int i, opt;
    char *colon;
    double total = 0, total_bytes = 0;
    int verbose = 0, single = 0;

    struct host_info hi = {host_default, port_default};

    struct worker_thread *workers;

    /* getopt */
    while ((opt = getopt(argc, argv, "h:p:c:n:vs")) != -1) {
        switch (opt) {
            case 'h':
                colon = strchr(optarg, ':');
                if(!colon) {
                    size_t sz = strlen(optarg);
                    hi.host = calloc(1 + sz, 1);
                    strncpy(hi.host, optarg, sz);
                } else {
                    hi.host = calloc(1+colon-optarg, 1);
                    strncpy(hi.host, optarg, colon-optarg);
                    hi.port = (short)atol(colon+1);
                }
                break;

            case 'p':
                hi.port = (short)atol(optarg);
                break;

            case 'c':
                thread_count = atoi(optarg);
                break;

            case 'n':
                msg_target = atoi(optarg);
                break;

            case 'v':
                verbose = 1;
                break;

            case 's':
                single = 1;
                thread_count = 1;
                break;
            default: /* '?' */
                usage(argv[0], host_default, port_default,
                        thread_count_default,
                        messages_default);
                exit(EXIT_FAILURE);
        }
    }

    /* run threads */
    workers = calloc(sizeof(struct worker_thread), thread_count);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (single) {
        printf("Single-threaded mode\n");
        workers[0].msg_target = msg_target;
        workers[0].hi = &hi;
        workers[0].verbose = verbose;
        workers[0].state = WS_INITIAL;
        worker_main(&workers[0]);
    } else {
        for (i = 0; i < thread_count; ++i) {
            workers[i].msg_target = msg_target;
            workers[i].hi = &hi;
            workers[i].verbose = verbose;
            workers[i].state = WS_INITIAL;

            pthread_create(&workers[i].thread, NULL,
                           worker_main, &workers[i]);
        }

        /* wait for threads to finish */
        for (i = 0; i < thread_count; ++i) {
            pthread_join(workers[i].thread, NULL);
            total += workers[i].msg_received;
            total_bytes += workers[i].byte_count;
        }
    }

    /* timing */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    float mili0 = t0.tv_sec * 1000 + t0.tv_nsec / 1000000;
    float mili1 = t1.tv_sec * 1000 + t1.tv_nsec / 1000000;

    if(total != 0) {
        printf("Read %ld messages in %0.2f sec: %0.2f msg/sec (%d MB/sec, %d KB/sec)\n",
            (long)total,
            (mili1-mili0)/1000.0,
            1000*total/(mili1-mili0),
            (int)(total_bytes / (1000*(mili1-mili0))),
            (int)(total_bytes / (mili1-mili0)));
        return EXIT_SUCCESS;
    } else {
        printf("No message was read.\n");
        return EXIT_FAILURE;
    }
}

