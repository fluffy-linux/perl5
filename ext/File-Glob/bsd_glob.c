/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guido van Rossum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include "bsd_glob.h"

/* Save File::Glob's BSD flag/error values before including system <glob.h>,
 * which defines its own glob_t and POSIX/GNU GLOB_* constants.
 */
enum {
    BG_APPEND     = GLOB_APPEND,
    BG_DOOFFS     = GLOB_DOOFFS,
    BG_ERR        = GLOB_ERR,
    BG_MARK       = GLOB_MARK,
    BG_NOCHECK    = GLOB_NOCHECK,
    BG_NOSORT     = GLOB_NOSORT,
    BG_ALTDIRFUNC = GLOB_ALTDIRFUNC,
    BG_BRACE      = GLOB_BRACE,
    BG_MAGCHAR    = GLOB_MAGCHAR,
    BG_NOMAGIC    = GLOB_NOMAGIC,
    BG_QUOTE      = GLOB_QUOTE,
    BG_TILDE_FL   = GLOB_TILDE,
    BG_NOCASE     = GLOB_NOCASE,
    BG_ALPHASORT  = GLOB_ALPHASORT,
    BG_LIMIT      = GLOB_LIMIT,
    BG_NOSPACE    = GLOB_NOSPACE,
    BG_ABEND      = GLOB_ABEND
};

#undef GLOB_APPEND
#undef GLOB_DOOFFS
#undef GLOB_ERR
#undef GLOB_MARK
#undef GLOB_NOCHECK
#undef GLOB_NOSORT
#undef GLOB_ALTDIRFUNC
#undef GLOB_BRACE
#undef GLOB_MAGCHAR
#undef GLOB_NOMAGIC
#undef GLOB_QUOTE
#undef GLOB_TILDE
#undef GLOB_NOCASE
#undef GLOB_ALPHASORT
#undef GLOB_LIMIT
#undef GLOB_NOSPACE
#undef GLOB_ABEND

#define glob_t sys_glob_t
#define glob64_t sys_glob64_t
#include <glob.h>
#undef glob_t
#undef glob64_t

#ifdef I_PWD
#	include <pwd.h>
#else
#if defined(HAS_PASSWD) && !defined(VMS)
        struct passwd *getpwnam(char *);
        struct passwd *getpwuid(Uid_t);
#endif
#endif

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN  PATH_MAX
#  else
#    define MAXPATHLEN  1024
#  endif
#endif

#include <limits.h>

#ifndef ARG_MAX
#  ifdef _SC_ARG_MAX
#    define     ARG_MAX         (sysconf(_SC_ARG_MAX))
#  else
#    ifdef _POSIX_ARG_MAX
#      define   ARG_MAX         _POSIX_ARG_MAX
#    else
#      ifdef WIN32
#        define ARG_MAX         14500
#      else
#        define ARG_MAX         4096
#      endif
#    endif
#  endif
#endif

#define BG_EOS          '\0'
#define BG_LBRACKET     '['
#define BG_NOT          '!'
#define BG_QUESTION     '?'
#define BG_QUOTE_CH     '\\'
#define BG_RANGE        '-'
#define BG_RBRACKET     ']'
#define BG_SEP          '/'
#ifdef DOSISH
#define BG_SEP2         '\\'
#endif
#define BG_STAR         '*'
#define BG_TILDE        '~'
#define BG_LBRACE       '{'
#define BG_RBRACE       '}'
#define BG_SLASH        '/'
#define BG_COMMA        ','

#ifndef GLOB_DEBUG
#define M_PROTECT       0x4000
#define M_ASCII         0x00ff
typedef U16 Char;
#else
#define M_PROTECT       0x40
#define M_ASCII         0x7f
typedef U8 Char;
#endif

#define CHAR(c)         ((Char)((c)&M_ASCII))

static int	 compare(const void *, const void *);
static int	 ci_compare(const void *, const void *);
static int	 g_Ctoc(const Char *, char *, STRLEN);
static Char	*g_strchr(Char *, int);
static int	 glob0(const Char *, glob_t *);
static int	 globextend(const Char *, glob_t *, size_t *);
static int	 globextend_cstr(const char *, glob_t *, size_t *);
static const Char *
                 globtilde(const Char *, Char *, size_t, glob_t *);
static int	 globexp1(const Char *, glob_t *);
static int	 globexp2(const Char *, const Char *, glob_t *, int *);

int
bsd_glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int), glob_t *pglob)
{
        const U8 *patnext;
        int c;
        Char *bufnext, *bufend, patbuf[MAXPATHLEN];

        patnext = (const U8 *) pattern;
        pglob->gl_pathc = 0;
        pglob->gl_pathv = NULL;
        pglob->gl_offs = 0;
        pglob->gl_flags = flags & ~BG_MAGCHAR;
        pglob->gl_errfunc = errfunc;
        pglob->gl_matchc = 0;

        bufnext = patbuf;
        bufend = bufnext + MAXPATHLEN - 1;
#ifdef DOSISH
        if (isalpha(pattern[0]) && pattern[1] == ':' &&
            pattern[2] != BG_SEP && pattern[2] != BG_SEP2 &&
            bufend - bufnext > 4) {
                *bufnext++ = pattern[0];
                *bufnext++ = ':';
                *bufnext++ = '.';
                *bufnext++ = BG_SEP;
                patnext += 2;
        }
#endif

        if (flags & BG_QUOTE) {
                /* Protect the quoted characters. */
                while (bufnext < bufend && (c = *patnext++) != BG_EOS)
                        if (c == BG_QUOTE_CH) {
#ifdef DOSISH
                                if ((c = *patnext++) != '[' && c != ']' &&
                                    c != '-' && c != '{' && c != '}' &&
                                    c != '~' && c != '\\') {
#else
                                if ((c = *patnext++) == BG_EOS) {
#endif
                                        c = BG_QUOTE_CH;
                                        --patnext;
                                }
                                *bufnext++ = (Char)(c | M_PROTECT);
                        } else
                                *bufnext++ = (Char)c;
        } else {
                while (bufnext < bufend && (c = *patnext++) != BG_EOS)
                        *bufnext++ = (Char)c;
        }
        *bufnext = BG_EOS;

        if (flags & BG_BRACE)
            return globexp1(patbuf, pglob);
        else
            return glob0(patbuf, pglob);
}

/*
 * Expand recursively a glob {} pattern. When there is no more expansion
 * invoke glob0 to resolve the leaf pattern via glibc glob(3).
 */
static int
globexp1(const Char *pattern, glob_t *pglob)
{
        const Char *ptr = pattern;
        int rv;

        /* Protect a single {}, for find(1), like csh */
        if (pattern[0] == BG_LBRACE && pattern[1] == BG_RBRACE && pattern[2] == BG_EOS)
                return glob0(pattern, pglob);

        while ((ptr = (const Char *) g_strchr((Char *) ptr, BG_LBRACE)) != NULL)
                if (!globexp2(ptr, pattern, pglob, &rv))
                        return rv;

        return glob0(pattern, pglob);
}

static int
globexp2(const Char *ptr, const Char *pattern,
         glob_t *pglob, int *rv)
{
        int     i;
        Char   *lm, *ls;
        const Char *pe, *pm, *pm1, *pl;
        Char    patbuf[MAXPATHLEN];

        for (lm = patbuf, pm = pattern; pm != ptr; *lm++ = *pm++)
                ;
        *lm = BG_EOS;
        ls = lm;

        /* Find the balanced brace */
        for (i = 0, pe = ++ptr; *pe; pe++)
                if (*pe == BG_LBRACKET) {
                        for (pm = pe++; *pe != BG_RBRACKET && *pe != BG_EOS; pe++)
                                ;
                        if (*pe == BG_EOS)
                                pe = pm;
                } else if (*pe == BG_LBRACE)
                        i++;
                else if (*pe == BG_RBRACE) {
                        if (i == 0)
                                break;
                        i--;
                }

        /* Non matching braces; just glob the pattern */
        if (i != 0 || *pe == BG_EOS) {
                *rv = glob0(patbuf, pglob);
                return 0;
        }

        for (i = 0, pl = pm = ptr; pm <= pe; pm++) {
                switch (*pm) {
                case BG_LBRACKET:
                        for (pm1 = pm++; *pm != BG_RBRACKET && *pm != BG_EOS; pm++)
                                ;
                        if (*pm == BG_EOS)
                                pm = pm1;
                        break;

                case BG_LBRACE:
                        i++;
                        break;

                case BG_RBRACE:
                        if (i) {
                                i--;
                                break;
                        }
                        /* FALLTHROUGH */
                case BG_COMMA:
                        if (i && *pm == BG_COMMA)
                                break;
                        else {
                                for (lm = ls; (pl < pm); *lm++ = *pl++)
                                        ;
                                for (pl = pe + 1; (*lm++ = *pl++) != BG_EOS; )
                                        ;
                                *rv = globexp1(patbuf, pglob);
                                if (*rv != 0)
                                        return 0;
                                pl = pm + 1;
                        }
                        break;

                default:
                        break;
                }
        }
        *rv = 0;
        return 0;
}

static const Char *
globtilde(const Char *pattern, Char *patbuf, size_t patbuf_len, glob_t *pglob)
{
        const char *h;
        const Char *p;
        Char *b, *eb;

        if (*pattern != BG_TILDE || !(pglob->gl_flags & BG_TILDE_FL))
                return pattern;

        eb = &patbuf[patbuf_len - 1];
        for (p = pattern + 1, b = patbuf;
             b < eb && *p && *p != BG_SLASH; *b++ = CHAR(*p++))
                ;
        *b = BG_EOS;

        if (patbuf[0] == BG_EOS) {
                if ((h = PerlEnv_getenv("HOME")) == NULL) {
#ifdef HAS_PASSWD
                        struct passwd *pwd;
                        if ((pwd = getpwuid(getuid())) == NULL)
                                return pattern;
                        else
                                h = pwd->pw_dir;
#elif defined(DOSISH)
                        if ((h = PerlEnv_getenv("USERPROFILE")) == NULL)
                                return pattern;
#else
                        return pattern;
#endif
                }
        } else {
#ifdef HAS_PASSWD
                char userbuf[MAXPATHLEN];
                struct passwd *pwd;
                if (g_Ctoc(patbuf, userbuf, sizeof(userbuf)))
                        return pattern;
                if ((pwd = getpwnam(userbuf)) == NULL)
                        return pattern;
                else
                        h = pwd->pw_dir;
#else
                return pattern;
#endif
        }

        for (b = patbuf; b < eb && *h; *b++ = (Char)(((U8)*h++) | M_PROTECT))
                ;
        while (b < eb && (*b++ = *p++) != BG_EOS)
                ;
        *b = BG_EOS;

        return patbuf;
}

static const Char *
find_bracket_end(const Char *qp)
{
        const Char *scan = qp + 1;

        if (*scan == BG_NOT)
                scan++;
        if (*scan == BG_EOS || CHAR(*scan) == BG_SEP)
                return NULL;
        for (scan++; *scan != BG_EOS; scan++) {
                if (CHAR(*scan) == BG_SEP)
                        return NULL;
                if (*scan == BG_RBRACKET)
                        return scan;
        }
        return NULL;
}

static int
glob0(const Char *pattern, glob_t *pglob)
{
        const Char *qpat, *qp;
        Char tildebuf[MAXPATHLEN];
        STRLEN qlen;
        char *cpat, *out;
        int oldflags, oldpathc, prev_wild_star;
        size_t limit = 0;
        int err = 0;

        qpat = globtilde(pattern, tildebuf, MAXPATHLEN, pglob);
        oldflags = pglob->gl_flags;
        oldpathc = pglob->gl_pathc;

        for (qp = qpat; *qp != BG_EOS; qp++)
                ;
        qlen = (STRLEN)(qp - qpat);

        Newx(cpat, 2 * qlen + 4, char);
        if (cpat == NULL)
                return BG_NOSPACE;

        out = cpat;
        prev_wild_star = 0;
        qp = qpat;
        while (*qp != BG_EOS) {
                Char c = *qp;
                U8 ch = (U8)(c & M_ASCII);

                if (c & M_PROTECT) {
                        prev_wild_star = 0;
                        if (ch == '\\' || ch == '*' || ch == '?' ||
                            ch == '[' || ch == ']')
                                *out++ = '\\';
                        *out++ = (char)ch;
                        qp++;
                        continue;
                }

                switch (ch) {
                case BG_QUOTE_CH:
                        prev_wild_star = 0;
                        *out++ = '\\';
                        *out++ = '\\';
                        qp++;
                        break;
                case BG_QUESTION:
                        prev_wild_star = 0;
                        pglob->gl_flags |= BG_MAGCHAR;
                        *out++ = '?';
                        qp++;
                        break;
                case BG_STAR:
                        pglob->gl_flags |= BG_MAGCHAR;
                        if (!prev_wild_star)
                                *out++ = '*';
                        prev_wild_star = 1;
                        qp++;
                        break;
                case BG_LBRACKET: {
                        const Char *endb = find_bracket_end(qp);
                        prev_wild_star = 0;
                        if (endb == NULL) {
                                *out++ = '\\';
                                *out++ = '[';
                                qp++;
                        } else {
                                int first_in_bkt = 1;
                                pglob->gl_flags |= BG_MAGCHAR;
                                *out++ = '[';
                                qp++;
                                if (*qp == BG_NOT) {
                                        *out++ = '!';
                                        qp++;
                                }
                                for (; qp < endb; qp++, first_in_bkt = 0) {
                                        U8 bc = (U8)(*qp & M_ASCII);
                                        int bprot = (*qp & M_PROTECT) != 0;
                                        if (first_in_bkt && (bc == '^' || bc == '!')) {
                                                /* bsd_glob only negates unescaped [!], never [^] or [\!] */
                                                *out++ = '\\';
                                                *out++ = (char)bc;
                                        } else if (bc == '\\') {
                                                *out++ = '\\';
                                                *out++ = '\\';
                                        } else if (bprot && bc == ']') {
                                                *out++ = '\\';
                                                *out++ = ']';
                                        } else if (bc == '[' && (qp + 1) < endb &&
                                                   ((qp[1] & M_ASCII) == ':' ||
                                                    (qp[1] & M_ASCII) == '.' ||
                                                    (qp[1] & M_ASCII) == '=')) {
                                                *out++ = '\\';
                                                *out++ = '[';
                                        } else if (bprot && bc == '-') {
                                                *out++ = '\\';
                                                *out++ = '-';
                                        } else {
                                                *out++ = (char)bc;
                                        }
                                }
                                *out++ = ']';
                                qp = endb + 1;
                        }
                        break;
                }
                default:
                        prev_wild_star = 0;
                        *out++ = (char)ch;
                        qp++;
                        break;
                }
        }
        *out = '\0';

        if (cpat[0] != '\0') {
                sys_glob_t sys_gl;
                int gflags = GLOB_NOSORT;
                int rc;
                size_t k;

#ifdef GLOB_KEEP_SLASHES
                gflags |= GLOB_KEEP_SLASHES;
#endif
                if (pglob->gl_flags & BG_MARK)
                        gflags |= GLOB_MARK;
                if (pglob->gl_flags & BG_ERR)
                        gflags |= GLOB_ERR;
#ifdef GLOB_CASEFOLD
                if (pglob->gl_flags & BG_NOCASE)
                        gflags |= GLOB_CASEFOLD;
#endif
                memset(&sys_gl, 0, sizeof(sys_gl));
                if ((pglob->gl_flags & BG_ALTDIRFUNC) && pglob->gl_opendir != NULL) {
                        gflags |= GLOB_ALTDIRFUNC;
                        sys_gl.gl_closedir = pglob->gl_closedir;
                        sys_gl.gl_readdir = (struct dirent *(*)(void *))pglob->gl_readdir;
                        sys_gl.gl_opendir = pglob->gl_opendir;
                        sys_gl.gl_lstat = (int (*)(const char *__restrict, struct stat *__restrict))pglob->gl_lstat;
                        sys_gl.gl_stat = (int (*)(const char *__restrict, struct stat *__restrict))pglob->gl_stat;
                }

                rc = glob(cpat, gflags, pglob->gl_errfunc, &sys_gl);
                for (k = 0; k < sys_gl.gl_pathc; k++) {
                        pglob->gl_matchc++;
                        err = globextend_cstr(sys_gl.gl_pathv[k], pglob, &limit);
                        if (err != 0)
                                break;
                }
                globfree(&sys_gl);

                if (err != 0) {
                        Safefree(cpat);
                        pglob->gl_flags = oldflags;
                        return err;
                }
                if (rc == GLOB_NOSPACE) {
                        Safefree(cpat);
                        pglob->gl_flags = oldflags;
                        return BG_NOSPACE;
                }
                if (rc == GLOB_ABORTED) {
                        Safefree(cpat);
                        pglob->gl_flags = oldflags;
                        return BG_ABEND;
                }
        }
        Safefree(cpat);

        if (pglob->gl_pathc == oldpathc &&
            ((pglob->gl_flags & BG_NOCHECK) ||
              ((pglob->gl_flags & BG_NOMAGIC) &&
               !(pglob->gl_flags & BG_MAGCHAR))))
        {
                pglob->gl_flags = oldflags;
                return globextend(qpat, pglob, &limit);
        }
        else if (!(pglob->gl_flags & BG_NOSORT)) {
                if (pglob->gl_pathv && pglob->gl_pathc > oldpathc)
                        qsort(pglob->gl_pathv + pglob->gl_offs + oldpathc,
                              pglob->gl_pathc - oldpathc, sizeof(char *),
                              (pglob->gl_flags & (BG_ALPHASORT | BG_NOCASE))
                                  ? ci_compare : compare);
        }
        pglob->gl_flags = oldflags;
        return 0;
}

static int
ci_compare(const void *p, const void *q)
{
        const char *pp = *(const char * const *)p;
        const char *qq = *(const char * const *)q;
        int ci;
        while (*pp && *qq) {
                if (toFOLD(*pp) != toFOLD(*qq))
                        break;
                ++pp;
                ++qq;
        }
        ci = toFOLD(*pp) - toFOLD(*qq);
        if (ci == 0)
                return compare(p, q);
        return ci;
}

static int
compare(const void *p, const void *q)
{
        return strcmp(*(const char * const *)p, *(const char * const *)q);
}

static int
globextend_cstr(const char *path, glob_t *pglob, size_t *limitp)
{
        char **pathv;
        int i;
        STRLEN newsize, len;
        char *copy;

        newsize = sizeof(*pathv) * (2 + pglob->gl_pathc + pglob->gl_offs);
        if (pglob->gl_pathv)
                pathv = Renew(pglob->gl_pathv, newsize, char*);
        else
                Newx(pathv, newsize, char*);
        if (pathv == NULL) {
                if (pglob->gl_pathv) {
                        Safefree(pglob->gl_pathv);
                        pglob->gl_pathv = NULL;
                }
                return BG_NOSPACE;
        }

        if (pglob->gl_pathv == NULL && pglob->gl_offs > 0) {
                pathv += pglob->gl_offs;
                for (i = pglob->gl_offs; --i >= 0; )
                        *--pathv = NULL;
        }
        pglob->gl_pathv = pathv;

        len = strlen(path) + 1;
        *limitp += len;
        Newx(copy, len, char);
        if (copy != NULL) {
                memcpy(copy, path, len);
                pathv[pglob->gl_offs + pglob->gl_pathc++] = copy;
        }
        pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;

        if ((pglob->gl_flags & BG_LIMIT) &&
            newsize + *limitp >= (unsigned long)ARG_MAX) {
                errno = 0;
                return BG_NOSPACE;
        }

        return (copy == NULL ? BG_NOSPACE : 0);
}

static int
globextend(const Char *path, glob_t *pglob, size_t *limitp)
{
        const Char *p;
        STRLEN len;
        char *buf;
        int rv;

        for (p = path; *p++; )
                ;
        len = (STRLEN)(p - path);
        Newx(buf, len, char);
        if (buf == NULL)
                return BG_NOSPACE;
        if (g_Ctoc(path, buf, len)) {
                Safefree(buf);
                return BG_NOSPACE;
        }
        rv = globextend_cstr(buf, pglob, limitp);
        Safefree(buf);
        return rv;
}

void
bsd_globfree(glob_t *pglob)
{
        int i;
        char **pp;

        if (pglob->gl_pathv != NULL) {
                pp = pglob->gl_pathv + pglob->gl_offs;
                for (i = pglob->gl_pathc; i--; ++pp)
                        if (*pp)
                                Safefree(*pp);
                Safefree(pglob->gl_pathv);
                pglob->gl_pathv = NULL;
        }
}

static Char *
g_strchr(Char *str, int ch)
{
        do {
                if (*str == ch)
                        return str;
        } while (*str++);
        return NULL;
}

static int
g_Ctoc(const Char *str, char *buf, STRLEN len)
{
        while (len--) {
                if ((*buf++ = (char)CHAR(*str++)) == BG_EOS)
                        return 0;
        }
        return 1;
}
