// The resolver as the kit's glibc-built libraries see it on bionic: glibc's struct addrinfo and constants, through
// resolver_compat.c's wrappers. Numeric lookups only: no Android resolver (netd) on a CI runner.
// Linked with -Wl,--wrap=<name> as link.flags says; prints "resolver ok" when all pass.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

struct glibc_addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct glibc_addrinfo *ai_next;
};

// What a glibc object's calls become with --wrap: the glibc code calls these names
int __wrap_getaddrinfo(const char *, const char *, const struct glibc_addrinfo *, struct glibc_addrinfo **);
void __wrap_freeaddrinfo(struct glibc_addrinfo *);
int __wrap_getnameinfo(const struct sockaddr *, socklen_t, char *, size_t, char *, size_t, int);
const char *__wrap_gai_strerror(int);

enum { GLIBC_AI_NUMERICHOST = 0x4, GLIBC_NI_NUMERICHOST = 1, GLIBC_NI_NUMERICSERV = 2, GLIBC_EAI_FAMILY = -6 };

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void numeric(const char *host, int family, const char *expected) {
    struct glibc_addrinfo hints = {.ai_flags = GLIBC_AI_NUMERICHOST, .ai_socktype = SOCK_STREAM}, *res = NULL;
    int rc = __wrap_getaddrinfo(host, "443", &hints, &res);
    CHECK(rc == 0);
    if (rc != 0) return;
    char text[64] = "";
    CHECK(res->ai_family == family);
    CHECK(res->ai_addr != NULL);
    if (res->ai_addr && family == AF_INET) {
        CHECK(ntohs(((struct sockaddr_in *)res->ai_addr)->sin_port) == 443);
        inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, text, sizeof text);
    } else if (res->ai_addr) {
        CHECK(ntohs(((struct sockaddr_in6 *)res->ai_addr)->sin6_port) == 443);
        inet_ntop(AF_INET6, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr, text, sizeof text);
    }
    printf("%s -> %s\n", host, text);
    CHECK(strcmp(text, expected) == 0);
    __wrap_freeaddrinfo(res);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    numeric("127.0.0.1", AF_INET, "127.0.0.1");
    numeric("::1", AF_INET6, "::1");

    // A bionic error comes back as glibc's code, and its message as bionic's for that code
    struct glibc_addrinfo hints = {.ai_family = 12345}, *res = NULL;
    int rc = __wrap_getaddrinfo("127.0.0.1", NULL, &hints, &res);
    printf("bad family: %d (%s)\n", rc, __wrap_gai_strerror(rc));
    CHECK(rc == GLIBC_EAI_FAMILY);

    // glibc's NI_NUMERICHOST | NI_NUMERICSERV (bionic's NI_NOFQDN | NI_NUMERICHOST otherwise)
    struct sockaddr_in sin = {.sin_family = AF_INET, .sin_port = htons(8080), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    char host[64] = "", service[16] = "";
    rc = __wrap_getnameinfo((struct sockaddr *)&sin, sizeof sin, host, sizeof host, service, sizeof service,
                            GLIBC_NI_NUMERICHOST | GLIBC_NI_NUMERICSERV);
    printf("getnameinfo: %d %s:%s\n", rc, host, service);
    CHECK(rc == 0 && strcmp(host, "127.0.0.1") == 0 && strcmp(service, "8080") == 0);

    puts(failures ? "resolver FAILED" : "resolver ok");
    return failures != 0;
}
