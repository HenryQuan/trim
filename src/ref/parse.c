/* parse.c -- string/buffer helpers + ast-grep --json=compact parsing */
#include <ctype.h>
#include <stdarg.h>

#include "../trim.h"
#include "rf.h"

char *rf_sdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---------- growable output buffer ---------- */

void rf_buf_addn(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8192;
        while (b->len + n + 1 > b->cap)
            b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}
void rf_buf_add(Buf *b, const char *s) { rf_buf_addn(b, s, strlen(s)); }
void rf_buf_addf(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    rf_buf_add(b, tmp);
}

/* ---------- json (ast-grep --json=compact) field extraction ---------- */

/* copy JSON string starting after opening quote, unescaping \" \\ \/ \n \r
   \t \b \f and \uXXXX (basic plane); returns malloc'd C string */
static char *json_unescape(const char *p, const char **end) {
    size_t cap = 64, n = 0;
    char *out = malloc(cap);
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'u': {
                unsigned v = 0;
                int i;
                for (i = 1; i <= 4 && isxdigit((unsigned char)p[i]); i++) {
                    char h = p[i];
                    v = v * 16 +
                        (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                p += i - 1;
                if (n + 4 > cap) {
                    cap *= 2;
                    out = realloc(out, cap);
                }
                if (v < 0x80) {
                    c = (char)v;
                } else if (v < 0x800) {
                    out[n++] = (char)(0xC0 | (v >> 6));
                    c = (char)(0x80 | (v & 0x3F));
                } else {
                    out[n++] = (char)(0xE0 | (v >> 12));
                    out[n++] = (char)(0x80 | ((v >> 6) & 0x3F));
                    c = (char)(0x80 | (v & 0x3F));
                }
                break;
            }
            default:
                c = *p;
                break;
            }
        }
        if (n + 2 > cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        out[n++] = c;
        p++;
    }
    out[n] = '\0';
    if (end)
        *end = *p == '"' ? p + 1 : p;
    return out;
}

static const char *find_key(const char *lo, const char *hi, const char *key) {
    size_t kl = strlen(key);
    for (const char *p = lo; p + kl < hi; p++)
        if (!strncmp(p, key, kl))
            return p + kl;
    return NULL;
}

/* parse matches array; objects start with {"text":" (9 chars) and the
   fields we need appear before the optional "metaVariables" member */
GrepHit *rf_parse_hits(const char *json, int *out_n) {
    static const char OBJ[] = "{\"text\":\"";
    GrepHit *hits = NULL;
    int n = 0, cap = 0;
    const char *p = json;
    while ((p = strstr(p, OBJ)) != NULL) {
        const char *obj = p;
        p += sizeof(OBJ) - 1;
        const char *nobj = strstr(p, "{\"text\":\"");
        const char *objEnd = nobj ? nobj : p + strlen(p);
        const char *meta = find_key(p, objEnd, "\"metaVariables\"");
        if (meta)
            objEnd = meta - strlen("\"metaVariables\"");
        GrepHit h = {0};
        h.text = json_unescape(p, NULL);
        const char *k = find_key(obj, objEnd, "\"start\":{\"line\":");
        if (k)
            h.start = strtol(k, NULL, 10);
        k = find_key(obj, objEnd, "\"end\":{\"line\":");
        if (k)
            h.end = strtol(k, NULL, 10);
        k = find_key(obj, objEnd, "\"file\":\"");
        if (k)
            h.file = json_unescape(k, NULL);
        if (h.file && h.text) {
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                hits = realloc(hits, (size_t)cap * sizeof(*hits));
            }
            hits[n++] = h;
        } else {
            free(h.text);
            free(h.file);
        }
        p = objEnd;
    }
    *out_n = n;
    return hits;
}
