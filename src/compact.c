#include "trim.h"

#define MAX_DICT   6            /* refs $1..$6 */
#define MIN_LEN    16           /* shortest value worth substituting */
#define SAVINGS_T  20           /* min net chars saved to keep an entry */

typedef struct { unsigned long long h; int pos; } HRec;

static int cmp_hrec(const void *a, const void *b) {
    const HRec *x = a, *y = b;
    if (x->h != y->h) return x->h < y->h ? -1 : 1;
    return x->pos - y->pos;
}

static int is_boundary(char c) {
    return c == '\0' || c == '/' || c == '\\' || c == ':' || c == ' ' || c == '\t' ||
           c == '\n' || c == '(' || c == ')' || c == ',' || c == ';' || c == '.' || c == '=';
}

static int region_free(const unsigned char *used, int p, int L) {
    for (int i = p; i < p + L; i++) if (used[i]) return 0;
    return 1;
}

/* true if the substring is bounded by separators on both sides (a whole token/phrase) */
static int aligned(const char *s, size_t n, int p, int L) {
    if (p > 0 && !is_boundary(s[p - 1])) return 0;
    if (p + L < (int)n && !is_boundary(s[p + L])) return 0;
    return 1;
}

/* find the best repeated substring (len>=minlen, occ>=2) fully inside unused regions.
   writes the value to out (<=outcap), returns its length (0 = none). */
static int find_repeated(const char *s, size_t n, const unsigned char *used,
                         int minlen, char *out, size_t outcap, int *occ) {
    static const int LENS[] = {64, 48, 40, 32, 24, 16, 12, 10};
    int best = 0, bestocc = 0, bestlen = 0, bestpos = -1;

    for (size_t li = 0; li < sizeof(LENS) / sizeof(LENS[0]); li++) {
        int L = LENS[li];
        if (L > (int)n || L < minlen) continue;
        int cnt = (int)n - L + 1;
        if (cnt <= 1) continue;
        HRec *rec = malloc((size_t)cnt * sizeof(HRec));
        if (!rec) continue;

        unsigned long long h = 0, base = 1;
        for (int i = 0; i < L; i++) { h = h * 131 + (unsigned char)s[i]; base *= 131; }
        rec[0].h = h; rec[0].pos = 0;
        for (int i = 1; i < cnt; i++) {
            h = h * 131 + (unsigned char)s[i + L - 1] - (unsigned char)s[i - 1] * base;
            rec[i].h = h; rec[i].pos = i;
        }
        qsort(rec, (size_t)cnt, sizeof(HRec), cmp_hrec);

        int i = 0;
        while (i < cnt) {
            int j = i;
            while (j + 1 < cnt && rec[j + 1].h == rec[i].h) j++;
            int p0 = rec[i].pos;
            if (region_free(used, p0, L) && aligned(s, n, p0, L)) {
                int occn = 0, cur = -1;
                for (int k = i; k <= j; k++) {
                    int pos = rec[k].pos;
                    if (memcmp(s + pos, s + p0, (size_t)L) == 0 &&
                        (cur < 0 || pos >= cur + L) &&
                        region_free(used, pos, L) && aligned(s, n, pos, L)) {
                        occn++; cur = pos;
                    }
                }
                if (occn >= 2) {
                    int sav = occn * (L - 2) - L - 6;
                    if (sav > best) { best = sav; bestocc = occn; bestlen = L; bestpos = p0; }
                }
            }
            i = j + 1;
        }
        free(rec);
    }

    if (!best) { *occ = 0; return 0; }
    if ((size_t)bestlen > outcap) bestlen = (int)outcap;
    memcpy(out, s + bestpos, (size_t)bestlen);
    out[bestlen] = '\0';
    *occ = bestocc;
    return bestlen;
}

/* lossless reference compaction: $1..$N = repeated substrings, body substitutes refs.
   all values come from the original text (no refs inside values => 1-hop), non-overlapping. */
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
                int len = find_repeated(s, n, used, MIN_LEN, out, sizeof(out) - 1, &occ);
                if (len == 0 || occ < 2) break;
                if (occ * (len - 2) - len - 6 <= SAVINGS_T) break;
                memcpy(valbuf[ndict], out, (size_t)len + 1);
                vlen[ndict] = len;
                /* mark all occurrences of the value used */
                for (size_t p = 0; p + (size_t)len <= n; ) {
                    if (memcmp(s + p, out, (size_t)len) == 0) {
                        for (int k = 0; k < len; k++) used[p + k] = 1;
                        p += (size_t)len;
                    } else p++;
                }
                ndict++;
            }
            free(used);
        }
    }

    if (ndict == 0) {
        char *r = malloc(n + 1);
        if (r) memcpy(r, s, n + 1);
        *out_n = n;
        return r ? r : (char*)s;
    }

    /* build body: substitute refs (longest value first) */
    size_t cap = n + 64 + (size_t)ndict * 300, on = 0;
    char *res = malloc(cap);
    for (int d = 0; d < ndict; d++) {
        int h = snprintf(res + on, cap - on, "$%d = %.*s\n", d + 1, vlen[d], valbuf[d]);
        on += (size_t)h;
    }
    res[on++] = '-'; res[on++] = '-'; res[on++] = '-'; res[on++] = '\n';

    /* order indices longest-first for substitution */
    int order[MAX_DICT];
    for (int d = 0; d < ndict; d++) order[d] = d;
    for (int a = 0; a < ndict; a++)
        for (int b = a + 1; b < ndict; b++)
            if (vlen[order[b]] > vlen[order[a]]) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    for (size_t i = 0; i < n; ) {
        int matched = -1;
        for (int oi = 0; oi < ndict; oi++) {
            int d = order[oi];
            if (i + (size_t)vlen[d] <= n && memcmp(s + i, valbuf[d], (size_t)vlen[d]) == 0) {
                matched = d; break;
            }
        }
        if (matched >= 0) {
            char ref[12];
            snprintf(ref, sizeof(ref), "$%d", matched + 1);
            size_t reflen = strlen(ref);
            if (on + reflen + 2 >= cap) { cap *= 2; res = realloc(res, cap); }
            memcpy(res + on, ref, reflen); on += reflen;
            i += (size_t)vlen[matched];
        } else {
            if (on + 2 >= cap) { cap *= 2; res = realloc(res, cap); }
            res[on++] = s[i++];
        }
    }
    res[on] = '\0';
    *out_n = on;
    return res;
}

/* shared for rg / sg / fd: compact output, cap only as a safety net */
void cmd_search(char **args, int argc) {
    char *out = run_cmd_capture(args, argc);
    if (!out) return;
    size_t cn = 0;
    char *comp = compact_paths(out, &cn);
    const size_t SEARCH_CAP = 8192;
    if (cn > SEARCH_CAP) {
        fwrite(comp, 1, SEARCH_CAP, stdout);
        printf("\n[TRUNCATED:%zu/%zu] prefer trim par\n", SEARCH_CAP, cn);
    } else {
        fwrite(comp, 1, cn, stdout);
        if (cn && comp[cn - 1] != '\n') fputc('\n', stdout);
    }
    free(comp);
    free(out);
}
