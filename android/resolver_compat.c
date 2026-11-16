// The resolver, for GraalVM's glibc-built libraries on bionic. A member of libglibc_compat.a of its own: linked only
// when the link wraps these calls (-Wl,--wrap=<name>, in the kit's link.flags), as its __real_* references need.
//
// bionic lays out struct addrinfo the BSD way (ai_canonname before ai_addr), and its AI_*, NI_* and EAI_* values are
// BSD's: the glibc code (libnet's InetAddress lookups) reaches getaddrinfo, freeaddrinfo, getnameinfo and gai_strerror
// through these wrappers, which translate.
#include <netdb.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

struct glibc_addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct glibc_addrinfo *ai_next;
};

// A node handed to the glibc code: its copy, then the bionic node it was made from (freed with it)
struct compat_node {
    struct glibc_addrinfo ai;
    struct addrinfo *bionic;
};

_Static_assert(offsetof(struct compat_node, ai) == 0, "a node is its glibc addrinfo");

// glibc's values
enum {
    GLIBC_AI_PASSIVE = 0x1, GLIBC_AI_CANONNAME = 0x2, GLIBC_AI_NUMERICHOST = 0x4, GLIBC_AI_V4MAPPED = 0x8,
    GLIBC_AI_ALL = 0x10, GLIBC_AI_ADDRCONFIG = 0x20, GLIBC_AI_NUMERICSERV = 0x400,
};
enum {
    GLIBC_NI_NUMERICHOST = 1, GLIBC_NI_NUMERICSERV = 2, GLIBC_NI_NOFQDN = 4, GLIBC_NI_NAMEREQD = 8, GLIBC_NI_DGRAM = 16,
};
enum {
    GLIBC_EAI_BADFLAGS = -1, GLIBC_EAI_NONAME = -2, GLIBC_EAI_AGAIN = -3, GLIBC_EAI_FAIL = -4, GLIBC_EAI_NODATA = -5,
    GLIBC_EAI_FAMILY = -6, GLIBC_EAI_SOCKTYPE = -7, GLIBC_EAI_SERVICE = -8, GLIBC_EAI_ADDRFAMILY = -9,
    GLIBC_EAI_MEMORY = -10, GLIBC_EAI_SYSTEM = -11, GLIBC_EAI_OVERFLOW = -12,
};

struct mapping { int glibc, bionic; };

static const struct mapping ai_flags[] = {
    {GLIBC_AI_PASSIVE, AI_PASSIVE}, {GLIBC_AI_CANONNAME, AI_CANONNAME}, {GLIBC_AI_NUMERICHOST, AI_NUMERICHOST},
    {GLIBC_AI_V4MAPPED, AI_V4MAPPED}, {GLIBC_AI_ALL, AI_ALL}, {GLIBC_AI_ADDRCONFIG, AI_ADDRCONFIG},
    {GLIBC_AI_NUMERICSERV, AI_NUMERICSERV},
};
static const struct mapping ni_flags[] = {
    {GLIBC_NI_NUMERICHOST, NI_NUMERICHOST}, {GLIBC_NI_NUMERICSERV, NI_NUMERICSERV}, {GLIBC_NI_NOFQDN, NI_NOFQDN},
    {GLIBC_NI_NAMEREQD, NI_NAMEREQD}, {GLIBC_NI_DGRAM, NI_DGRAM},
};
static const struct mapping eai_codes[] = {
    {GLIBC_EAI_BADFLAGS, EAI_BADFLAGS}, {GLIBC_EAI_NONAME, EAI_NONAME}, {GLIBC_EAI_AGAIN, EAI_AGAIN},
    {GLIBC_EAI_FAIL, EAI_FAIL}, {GLIBC_EAI_NODATA, EAI_NODATA}, {GLIBC_EAI_FAMILY, EAI_FAMILY},
    {GLIBC_EAI_SOCKTYPE, EAI_SOCKTYPE}, {GLIBC_EAI_SERVICE, EAI_SERVICE}, {GLIBC_EAI_ADDRFAMILY, EAI_ADDRFAMILY},
    {GLIBC_EAI_MEMORY, EAI_MEMORY}, {GLIBC_EAI_SYSTEM, EAI_SYSTEM}, {GLIBC_EAI_OVERFLOW, EAI_OVERFLOW},
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int flags_to_bionic(const struct mapping *table, size_t count, int flags) {
    int result = 0;
    for (size_t i = 0; i < count; i++) if (flags & table[i].glibc) result |= table[i].bionic;
    return result;
}

static int flags_to_glibc(const struct mapping *table, size_t count, int flags) {
    int result = 0;
    for (size_t i = 0; i < count; i++) if (flags & table[i].bionic) result |= table[i].glibc;
    return result;
}

static int eai_to_glibc(int code) {
    if (code == 0) return 0;
    for (size_t i = 0; i < COUNT(eai_codes); i++)
        if (eai_codes[i].bionic == code) return eai_codes[i].glibc;
    return GLIBC_EAI_FAIL;
}

static int eai_to_bionic(int code) {
    for (size_t i = 0; i < COUNT(eai_codes); i++)
        if (eai_codes[i].glibc == code) return eai_codes[i].bionic;
    return code;
}

int __real_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void __real_freeaddrinfo(struct addrinfo *);
int __real_getnameinfo(const struct sockaddr *, socklen_t, char *, size_t, char *, size_t, int);
const char *__real_gai_strerror(int);

void __wrap_freeaddrinfo(struct addrinfo *res) {
    struct compat_node *node = (struct compat_node *)res;
    if (node == NULL) return;
    // From that node on, as glibc (the bionic list from its node on, which owns the addresses and names)
    struct addrinfo *bionic = node->bionic;
    while (node != NULL) {
        struct compat_node *next = (struct compat_node *)node->ai.ai_next;
        free(node);
        node = next;
    }
    __real_freeaddrinfo(bionic);
}

int __wrap_getaddrinfo(const char *host, const char *service, const struct addrinfo *glibc_hints, struct addrinfo **res) {
    struct addrinfo hints, *bionic = NULL;
    if (glibc_hints != NULL) {
        const struct glibc_addrinfo *h = (const struct glibc_addrinfo *)glibc_hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = flags_to_bionic(ai_flags, COUNT(ai_flags), h->ai_flags);
        hints.ai_family = h->ai_family;
        hints.ai_socktype = h->ai_socktype;
        hints.ai_protocol = h->ai_protocol;
    }
    int result = __real_getaddrinfo(host, service, glibc_hints != NULL ? &hints : NULL, &bionic);
    if (result != 0) return eai_to_glibc(result);

    struct compat_node *head = NULL, **tail = &head;
    for (struct addrinfo *ai = bionic; ai != NULL; ai = ai->ai_next) {
        struct compat_node *node = malloc(sizeof(*node));
        if (node == NULL) {
            // The nodes made so far, and the whole bionic list (the first node's), go
            if (head != NULL) __wrap_freeaddrinfo((struct addrinfo *)head);
            else __real_freeaddrinfo(bionic);
            return GLIBC_EAI_MEMORY;
        }
        node->ai = (struct glibc_addrinfo){
            .ai_flags = flags_to_glibc(ai_flags, COUNT(ai_flags), ai->ai_flags),
            .ai_family = ai->ai_family,
            .ai_socktype = ai->ai_socktype,
            .ai_protocol = ai->ai_protocol,
            .ai_addrlen = ai->ai_addrlen,
            .ai_addr = ai->ai_addr,
            .ai_canonname = ai->ai_canonname,
        };
        node->bionic = ai;
        *tail = node;
        tail = (struct compat_node **)&node->ai.ai_next;
    }
    *res = (struct addrinfo *)head;
    return 0;
}

int __wrap_getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, size_t hostlen, char *service,
                       size_t servicelen, int flags) {
    return eai_to_glibc(__real_getnameinfo(addr, addrlen, host, hostlen, service, servicelen,
                                           flags_to_bionic(ni_flags, COUNT(ni_flags), flags)));
}

const char *__wrap_gai_strerror(int code) { return __real_gai_strerror(eai_to_bionic(code)); }
