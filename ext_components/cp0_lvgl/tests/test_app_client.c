/*
 * The app side of launcher.sock (report 035): the socket path, "no service" answers at once, ok / err replies, a silent
 * server costs at most the timeout, over-long and malformed lines never leave the client, a reply that is not ours.
 * The servers here are tiny forked fakes; the real server's tests are in projects/APPLaunch/tests/test_app_request_server.cpp.
 */
#define _GNU_SOURCE

#include "cp0_app_client.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int listen_at(const char *path)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    unlink(path);
    CHECK(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
    CHECK(listen(fd, 4) == 0);
    return fd;
}

/* Child: accepts one connection, reads a line, answers per `mode`: 'e' echoes "ok <line>", 'x' "err nosound", 's' says
 * nothing for 2 s, 'g' garbage without newline, 'l' a reply line of 400 bytes. */
static pid_t fake_server(int lfd, char mode)
{
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid != 0) return pid;
    const int c = accept(lfd, NULL, NULL);
    char line[512];
    ssize_t n = recv(c, line, sizeof(line) - 1, 0);
    if (n < 0) n = 0;
    line[n] = '\0';
    char out[600];
    if (mode == 'e') {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        snprintf(out, sizeof(out), "ok got:%s\n", line);
        (void)!write(c, out, strlen(out));
    } else if (mode == 'x') {
        (void)!write(c, "err nosound\n", 12);
    } else if (mode == 's') {
        sleep(2);
    } else if (mode == 'g') {
        (void)!write(c, "ok but no newline", 17);
        sleep(2);
    } else if (mode == 'l') {
        memset(out, 'a', 400);
        out[0] = 'o';
        out[1] = 'k';
        out[2] = ' ';
        out[400] = '\n';
        (void)!write(c, out, 401);
    }
    _exit(0);
}

int main(void)
{
    char root[] = "/tmp/cp0-client-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[256], reply[256];

    /* the path: $APPLAUNCH_SOCKET, else $XDG_RUNTIME_DIR/applaunch/launcher.sock, else no service */
    unsetenv(CP0_APP_CLIENT_ENV_SOCKET);
    unsetenv("XDG_RUNTIME_DIR");
    CHECK(cp0_app_client_path(path, sizeof(path)) == -1);
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 0) == -1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    CHECK(cp0_app_client_path(path, sizeof(path)) == 0 && strcmp(path, "/run/user/1000/applaunch/launcher.sock") == 0);
    char small[8];
    CHECK(cp0_app_client_path(small, sizeof(small)) == -1);
    unsetenv("XDG_RUNTIME_DIR");
    snprintf(path, sizeof(path), "%s/s.sock", root);
    setenv(CP0_APP_CLIENT_ENV_SOCKET, path, 1);

    /* no socket, a stale socket file, a path too long: -1 at once, nothing printed */
    long long t = now_ms();
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 0) == -1 && reply[0] == '\0');
    int fd = listen_at(path);
    close(fd); /* the file stays: ECONNREFUSED */
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 0) == -1);
    CHECK(now_ms() - t < 100);
    char longpath[400];
    memset(longpath, 'a', sizeof(longpath) - 1);
    longpath[sizeof(longpath) - 1] = '\0';
    setenv(CP0_APP_CLIENT_ENV_SOCKET, longpath, 1);
    CHECK(cp0_app_client_path(path, sizeof(path)) == -1);
    snprintf(path, sizeof(path), "%s/s.sock", root);
    setenv(CP0_APP_CLIENT_ENV_SOCKET, path, 1);

    /* ok, err, the line as sent */
    fd = listen_at(path);
    pid_t child = fake_server(fd, 'e');
    CHECK(cp0_app_client_call("beep done", reply, sizeof(reply), 0) == 0);
    CHECK(strcmp(reply, "ok got:beep done") == 0);
    waitpid(child, NULL, 0);
    child = fake_server(fd, 'x');
    CHECK(cp0_app_client_call("beep", reply, sizeof(reply), 0) == 1 && strcmp(reply, "err nosound") == 0);
    waitpid(child, NULL, 0);
    child = fake_server(fd, 'e');
    CHECK(cp0_app_beep("warn") == 0);
    waitpid(child, NULL, 0);
    child = fake_server(fd, 'x');
    CHECK(cp0_app_beep(NULL) == 1);
    waitpid(child, NULL, 0);
    child = fake_server(fd, 'e');
    CHECK(cp0_app_toast("two\nlines") == 0); /* a newline would end the request: it becomes a space */
    waitpid(child, NULL, 0);

    /* a reply without a newline, a reply that is too long, a silent server: -1, within the timeout */
    child = fake_server(fd, 'g');
    t = now_ms();
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 300) == -1);
    CHECK(now_ms() - t < 800);
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    child = fake_server(fd, 'l');
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 300) == -1);
    waitpid(child, NULL, 0);
    child = fake_server(fd, 's');
    t = now_ms();
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 0) == -1);
    CHECK(now_ms() - t >= 150 && now_ms() - t < 600); /* the default timeout, 200 ms */
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);

    /* requests that never leave: empty, with a newline, longer than the line limit, NULL */
    CHECK(cp0_app_client_call("", reply, sizeof(reply), 0) == -1);
    CHECK(cp0_app_client_call("a\nb", reply, sizeof(reply), 0) == -1);
    char big[400];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK(cp0_app_client_call(big, reply, sizeof(reply), 0) == -1);
    CHECK(cp0_app_client_call(NULL, reply, sizeof(reply), 0) == -1);
    CHECK(cp0_app_toast("") == -1 && cp0_app_toast(NULL) == -1);
    CHECK(cp0_app_beep("a-name-that-is-much-too-long-for-this") == -1);

    close(fd);
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
    printf("test_app_client: PASS\n");
    return 0;
}
