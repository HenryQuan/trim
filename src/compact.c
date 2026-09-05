#include "trim.h"

#define MAX_DICT                                                               \
    5 /* refs $1..$5 — keep more repeated context in one result              \
       */
#define MIN_LEN                                                                \
    8 /* catch genuinely-reused short phrases like "pub(crate) fn " */
#define SAVINGS_T 0 /* keep any entry with positive net savings */

typedef struct {
    unsigned long long h;
    int pos;
} HRec;

static int cmp_hrec(const void *a, const void *b) {
    const HRec *x = a, *y = b;
    if (x->h != y->h)
        return x->h < y->h ? -1 : 1;
    return x->pos - y->pos;
}

static int is_boundary(char c) {
    return c == '\0' || c == '/' || c == '\\' || c == ':' || c == ' ' ||
           c == '\t' || c == '\n' || c == '(' || c == ')' || c == ',' ||
           c == ';' || c == '.' || c == '=';
}

static int region_free(const unsigned char *used, int p, int L) {
    for (int i = p; i < p + L; i++)
        if (used[i])
            return 0;
    return 1;
}

/* find the best repeated substring (len>=minlen, occ>=2) in unused regions.
   start must follow a separator; the end is the natural longest-common-prefix
   cut where the occurrences diverge. writes the value to out (<=outcap),
   returns len (0=none). */
static int find_repeated(const char *s, size_t n, const unsigned char *used,
                         int minlen, char *out, size_t outcap, int *occ) {
    static const int LENS[] = {64, 48, 40, 32, 24, 16, 12, 10};
    int best = 0, bestocc = 0, bestlen = 0, bestpos = -1;

    for (size_t li = 0; li < sizeof(LENS) / sizeof(LENS[0]); li++) {
        int L = LENS[li];
        if (L > (int)n || L < minlen)
            continue;
        int cnt = (int)n - L + 1;
        if (cnt <= 1)
            continue;
        HRec *rec = malloc((size_t)cnt * sizeof(HRec));
        if (!rec)
            continue;

        unsigned long long h = 0, base = 1;
        for (int i = 0; i < L; i++) {
            h = h * 131 + (unsigned char)s[i];
            base *= 131;
        }
        rec[0].h = h;
        rec[0].pos = 0;
        for (int i = 1; i < cnt; i++) {
            h = h * 131 + (unsigned char)s[i + L - 1] -
                (unsigned char)s[i - 1] * base;
            rec[i].h = h;
            rec[i].pos = i;
        }
        qsort(rec, (size_t)cnt, sizeof(HRec), cmp_hrec);

        int i = 0;
        while (i < cnt) {
            int j = i;
            while (j + 1 < cnt && rec[j + 1].h == rec[i].h)
                j++;
            if (j - i + 1 > 4096) {
                i = j + 1;
                continue;
            } /* degenerate (all-same text) */
            int p0 = rec[i].pos;
            if (p0 > 0 && !is_boundary(s[p0 - 1])) {
                i = j + 1;
                continue;
            }
            if (s[p0] == ' ' || s[p0] == '\t') {
                i = j + 1;
                continue;
            } /* no leading whitespace */
            if (!region_free(used, p0, L)) {
                i = j + 1;
                continue;
            }

            int minlcp = (int)n - p0, occn = 0, last = -1;
            for (int k = i; k <= j; k++) {
                int pos = rec[k].pos;
                if (last >= 0 && pos < last)
                    continue;
                if (pos > 0 && !is_boundary(s[pos - 1]))
                    continue;
                if (!region_free(used, pos, L))
                    continue;
                if (memcmp(s + pos, s + p0, (size_t)L) != 0)
                    continue; /* hash collision guard */
                int c = 0;
                while (c < minlcp && c < 512 && pos + c < (int)n &&
                       s[pos + c] == s[p0 + c])
                    c++;
                if (c < minlcp)
                    minlcp = c;
                occn++;
                last = pos + (pos == p0 ? L : c);
            }
            if (occn >= 2 && minlcp >= minlen) {
                /* value must not span a newline (else refs swallow line breaks)
                 */
                const char *nlp = memchr(s + p0, '\n', (size_t)minlcp);
                int Lv = nlp ? (int)(nlp - (s + p0)) : minlcp;
                while (Lv >= minlen && (s[p0 + Lv - 1] == ' ' ||
                                        s[p0 + Lv - 1] == '\t'))
                    Lv--; /* refs must not swallow the inter-word space */
                if (Lv >= minlen) {
                    int sav = occn * (Lv - 2); /* body savings only; the small
                                                  dict header is not a cost */
                    if (sav > best) {
                        best = sav;
                        bestocc = occn;
                        bestlen = Lv;
                        bestpos = p0;
                    }
                }
            }
            i = j + 1;
        }
        free(rec);
    }

    if (!best) {
        *occ = 0;
        return 0;
    }
    if ((size_t)bestlen > outcap)
        bestlen = (int)outcap;
    memcpy(out, s + bestpos, (size_t)bestlen);
    out[bestlen] = '\0';
    *occ = bestocc;
    return bestlen;
}

/* count non-overlapping occurrences of needle in hay */
static int count_sub(const char *hay, const char *needle) {
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn == 0)
        return 0;
    int c = 0;
    for (size_t i = 0; i + nn <= hn;) {
        if (memcmp(hay + i, needle, nn) == 0) {
            c++;
            i += nn;
        } else
            i++;
    }
    return c;
}

/* build the body of s with every dict value replaced by its $N reference
 * (longest first) */
static char *build_body(const char *s, size_t n, char valbuf[][257], int vlen[],
                        int ndict) {
    int order[MAX_DICT];
    for (int d = 0; d < ndict; d++)
        order[d] = d;
    for (int a = 0; a < ndict; a++)
        for (int b = a + 1; b < ndict; b++)
            if (vlen[order[b]] > vlen[order[a]]) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }

    size_t cap = n + 64, on = 0;
    char *res = malloc(cap);
    for (size_t i = 0; i < n;) {
        int m = -1;
        for (int oi = 0; oi < ndict; oi++) {
            int d = order[oi];
            if (i + (size_t)vlen[d] <= n &&
                memcmp(s + i, valbuf[d], (size_t)vlen[d]) == 0) {
                m = d;
                break;
            }
        }
        if (m >= 0) {
            char ref[16];
            // write ^N^ for the reference (to avoid accidental $N in the body)
            snprintf(ref, sizeof(ref), "^%d^", m + 1);
            size_t rl = strlen(ref);
            if (on + rl + 2 >= cap) {
                cap *= 2;
                res = realloc(res, cap);
            }
            memcpy(res + on, ref, rl);
            on += rl;
            i += (size_t)vlen[m];
        } else {
            if (on + 2 >= cap) {
                cap *= 2;
                res = realloc(res, cap);
            }
            res[on++] = s[i++];
        }
    }
    res[on] = '\0';
    return res;
}

/* lossless reference compaction: $1..$N = repeated substrings, body substitutes
   refs. greedy by real savings; a post-pass drops any ref used fewer than 2
   times. */
char *compact_paths(const char *s, size_t *out_n) {
    size_t n = strlen(s);
    char valbuf[MAX_DICT][257];
    int vlen[MAX_DICT] = {0};
    int ndict = 0;

    if (n > 0) {
        unsigned char *used = calloc(n, 1);
        if (used) {
            while (ndict < MAX_DICT) {
                char out[257];
                int occ = 0;
                int len = find_repeated(s, n, used, MIN_LEN, out,
                                        sizeof(out) - 1, &occ);
                if (len == 0 || occ < 2)
                    break;
                if (occ * (len - 2) <= SAVINGS_T)
                    break;
                memcpy(valbuf[ndict], out, (size_t)len + 1);
                vlen[ndict] = len;
                for (size_t p = 0; p + (size_t)len <= n;) {
                    if (memcmp(s + p, out, (size_t)len) == 0) {
                        for (int k = 0; k < len; k++)
                            used[p + k] = 1;
                        p += (size_t)len;
                    } else
                        p++;
                }
                ndict++;
            }
            free(used);
        }
    }

    /* post-pass: drop refs that appear fewer than 2 times in the final body (a
     * net loss) */
    char *body = NULL;
    for (;;) {
        if (body)
            free(body);
        body = build_body(s, n, valbuf, vlen, ndict);
        int bad[MAX_DICT] = {0}, nbad = 0;
        for (int d = 0; d < ndict; d++) {
            char ref[8];
            snprintf(ref, sizeof(ref), "^%d^", d + 1);
            if (count_sub(body, ref) < 2) {
                bad[d] = 1;
                nbad++;
            }
        }
        if (nbad == 0)
            break;
        int w = 0;
        for (int d = 0; d < ndict; d++)
            if (!bad[d]) {
                if (w != d) {
                    memcpy(valbuf[w], valbuf[d], 257);
                    vlen[w] = vlen[d];
                }
                w++;
            }
        ndict = w;
        if (ndict == 0) {
            free(body);
            body = NULL;
            break;
        }
    }

    if (ndict == 0 || !body) {
        char *r = malloc(n + 1);
        if (r)
            memcpy(r, s, n + 1);
        free(body);
        *out_n = n;
        return r ? r : (char *)s;
    }

    size_t blen = strlen(body);
    size_t cap = blen + 64 + (size_t)ndict * 300, on = 0;
    char *res = malloc(cap);
    for (int d = 0; d < ndict; d++) {
        int h = snprintf(res + on, cap - on, "^%d^ = %.*s\n", d + 1, vlen[d],
                         valbuf[d]);
        on += (size_t)h;
    }
    res[on++] = '-';
    res[on++] = '-';
    res[on++] = '-';
    res[on++] = '\n';
    if (on + blen + 2 >= cap) {
        cap = on + blen + 64;
        res = realloc(res, cap);
    }
    memcpy(res + on, body, blen);
    on += blen;
    res[on] = '\0';
    *out_n = on;
    free(body);
    return res;
}

/* shared for rg / sg / fd: compact output, cap only as a safety net
 * (line-aligned) */
void cmd_search(const char *const *args, int argc) {
    char *out = run_cmd_capture(args, argc);
    if (!out)
        return;
    size_t cn = 0;
    char *comp = compact_paths(out, &cn);
    const size_t SEARCH_CAP = 8192;
    if (cn > SEARCH_CAP) {
        /* cut at a line boundary, not mid-line */
        size_t cut = SEARCH_CAP;
        while (cut > 0 && comp[cut - 1] != '\n')
            cut--;
        if (cut == 0)
            cut = SEARCH_CAP; /* no newline within cap; hard cut */
        fwrite(comp, 1, cut, stdout);
        printf("\n[TRUNCATED:%zu/%zu] %s\n", cut, cn, pick_hint_ctx("rg "));
    } else {
        fwrite(comp, 1, cn, stdout);
        if (cn && comp[cn - 1] != '\n')
            fputc('\n', stdout);
    }
    free(comp);
    free(out);
}
