/*
  A page that stops reading must not stop the client.

  webui.c writes to the page's sockets with blocking send(): the live
  stream (SSE) and every response. On a desktop the browser always
  reads. On Android the app's own process, the one with the WebView, is
  frozen a few seconds after it leaves the screen; the stream's socket
  buffer then fills and send() blocks the client's only loop -- no
  telemetry, no answer to the console's discovery, no unlocks -- until
  the app is opened again.

  Here a send on a connected socket waits at most SEND_WAIT_MS for room.
  A reader that does not drain in that time gets a failed send, which
  webui.c already handles: the stream is dropped (the page reconnects
  when it wakes up) or the response is cut short.

  Bionic turns send() into sendto(..., NULL, 0), and into __sendto_chk()
  when the buffer's size is known, so all three are wrapped. Datagrams
  (a destination address: the console's UDP traffic) go out untouched,
  and status.c reads the console's RAA1 answers from them.
*/
#include "glue.h"

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#define SEND_WAIT_MS 250

ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags,
                      const struct sockaddr *to, socklen_t tolen);
ssize_t __real___sendto_chk(int fd, const void *buf, size_t len, size_t buf_size, int flags,
                            const struct sockaddr *to, socklen_t tolen);

static int writable(int fd)
{
    struct pollfd p;

    p.fd = fd;
    p.events = POLLOUT;
    p.revents = 0;
    return poll(&p, 1, SEND_WAIT_MS) > 0;
}

ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags,
                      const struct sockaddr *to, socklen_t tolen)
{
    ssize_t n;

    if (to != NULL) {
        n = __real_sendto(fd, buf, len, flags, to, tolen);
        status_datagram(buf, len);
        return n;
    }
    for (;;) {
        n = __real_sendto(fd, buf, len, flags | MSG_DONTWAIT, NULL, 0);
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            return n;
        if (!writable(fd)) {
            errno = EAGAIN;
            return -1;
        }
    }
}

ssize_t __wrap___sendto_chk(int fd, const void *buf, size_t len, size_t buf_size, int flags,
                            const struct sockaddr *to, socklen_t tolen)
{
    ssize_t n;

    if (to != NULL) {
        n = __real___sendto_chk(fd, buf, len, buf_size, flags, to, tolen);
        status_datagram(buf, len);
        return n;
    }
    for (;;) {
        n = __real___sendto_chk(fd, buf, len, buf_size, flags | MSG_DONTWAIT, NULL, 0);
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            return n;
        if (!writable(fd)) {
            errno = EAGAIN;
            return -1;
        }
    }
}

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags)
{
    return __wrap_sendto(fd, buf, len, flags, NULL, 0);
}
