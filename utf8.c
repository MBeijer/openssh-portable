/* $OpenBSD: utf8.c,v 1.8 2018/08/21 13:56:27 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Utility functions for multibyte-character handling,
 * in particular to sanitize untrusted strings for terminal output.
 */

#include "includes.h"

#include <sys/types.h>
#ifdef HAVE_LANGINFO_H
# include <langinfo.h>
#endif
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(HAVE_STRNVIS) && defined(HAVE_VIS_H) && !defined(BROKEN_STRNVIS)
# include <vis.h>
#endif
#ifdef HAVE_WCHAR_H
# include <wchar.h>
#endif

#include "utf8.h"
/*
 * This is an implementation of wcwidth() and wcswidth() as defined in
 * "The Single UNIX Specification, Version 2, The Open Group, 1997"
 * <http://www.UNIX-systems.org/online.html>
 *
 * Markus Kuhn -- 2001-09-08 -- public domain
 */

#include <wchar.h>

struct interval {
  unsigned short first;
  unsigned short last;
};

/* auxiliary function for binary search in interval table */
static int bisearch(wchar_t ucs, const struct interval *table, int max) {
  int min = 0;
  int mid;

  if (ucs < table[0].first || ucs > table[max].last)
    return 0;
  while (max >= min) {
    mid = (min + max) / 2;
    if (ucs > table[mid].last)
      min = mid + 1;
    else if (ucs < table[mid].first)
      max = mid - 1;
    else
      return 1;
  }

  return 0;
}


/* The following functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      FullWidth (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 * This implementation assumes that wchar_t characters are encoded
 * in ISO 10646.
 */

int wcwidth(wchar_t ucs)
{
  /* sorted list of non-overlapping intervals of non-spacing characters */
  static const struct interval combining[] = {
    { 0x0300, 0x034E }, { 0x0360, 0x0362 }, { 0x0483, 0x0486 },
    { 0x0488, 0x0489 }, { 0x0591, 0x05A1 }, { 0x05A3, 0x05B9 },
    { 0x05BB, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 },
    { 0x05C4, 0x05C4 }, { 0x064B, 0x0655 }, { 0x0670, 0x0670 },
    { 0x06D6, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED },
    { 0x070F, 0x070F }, { 0x0711, 0x0711 }, { 0x0730, 0x074A },
    { 0x07A6, 0x07B0 }, { 0x0901, 0x0902 }, { 0x093C, 0x093C },
    { 0x0941, 0x0948 }, { 0x094D, 0x094D }, { 0x0951, 0x0954 },
    { 0x0962, 0x0963 }, { 0x0981, 0x0981 }, { 0x09BC, 0x09BC },
    { 0x09C1, 0x09C4 }, { 0x09CD, 0x09CD }, { 0x09E2, 0x09E3 },
    { 0x0A02, 0x0A02 }, { 0x0A3C, 0x0A3C }, { 0x0A41, 0x0A42 },
    { 0x0A47, 0x0A48 }, { 0x0A4B, 0x0A4D }, { 0x0A70, 0x0A71 },
    { 0x0A81, 0x0A82 }, { 0x0ABC, 0x0ABC }, { 0x0AC1, 0x0AC5 },
    { 0x0AC7, 0x0AC8 }, { 0x0ACD, 0x0ACD }, { 0x0B01, 0x0B01 },
    { 0x0B3C, 0x0B3C }, { 0x0B3F, 0x0B3F }, { 0x0B41, 0x0B43 },
    { 0x0B4D, 0x0B4D }, { 0x0B56, 0x0B56 }, { 0x0B82, 0x0B82 },
    { 0x0BC0, 0x0BC0 }, { 0x0BCD, 0x0BCD }, { 0x0C3E, 0x0C40 },
    { 0x0C46, 0x0C48 }, { 0x0C4A, 0x0C4D }, { 0x0C55, 0x0C56 },
    { 0x0CBF, 0x0CBF }, { 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD },
    { 0x0D41, 0x0D43 }, { 0x0D4D, 0x0D4D }, { 0x0DCA, 0x0DCA },
    { 0x0DD2, 0x0DD4 }, { 0x0DD6, 0x0DD6 }, { 0x0E31, 0x0E31 },
    { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E }, { 0x0EB1, 0x0EB1 },
    { 0x0EB4, 0x0EB9 }, { 0x0EBB, 0x0EBC }, { 0x0EC8, 0x0ECD },
    { 0x0F18, 0x0F19 }, { 0x0F35, 0x0F35 }, { 0x0F37, 0x0F37 },
    { 0x0F39, 0x0F39 }, { 0x0F71, 0x0F7E }, { 0x0F80, 0x0F84 },
    { 0x0F86, 0x0F87 }, { 0x0F90, 0x0F97 }, { 0x0F99, 0x0FBC },
    { 0x0FC6, 0x0FC6 }, { 0x102D, 0x1030 }, { 0x1032, 0x1032 },
    { 0x1036, 0x1037 }, { 0x1039, 0x1039 }, { 0x1058, 0x1059 },
    { 0x1160, 0x11FF }, { 0x17B7, 0x17BD }, { 0x17C6, 0x17C6 },
    { 0x17C9, 0x17D3 }, { 0x180B, 0x180E }, { 0x18A9, 0x18A9 },
    { 0x200B, 0x200F }, { 0x202A, 0x202E }, { 0x206A, 0x206F },
    { 0x20D0, 0x20E3 }, { 0x302A, 0x302F }, { 0x3099, 0x309A },
    { 0xFB1E, 0xFB1E }, { 0xFE20, 0xFE23 }, { 0xFEFF, 0xFEFF },
    { 0xFFF9, 0xFFFB }
  };

  /* test for 8-bit control characters */
  if (ucs == 0)
    return 0;
  if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
    return -1;

  /* binary search in table of non-spacing characters */
  if (bisearch(ucs, combining,
	       sizeof(combining) / sizeof(struct interval) - 1))
    return 0;

  /* if we arrive here, ucs is not a combining or C0/C1 control character */

  return 1 + 
    (ucs >= 0x1100 &&
     (ucs <= 0x115f ||                    /* Hangul Jamo init. consonants */
      (ucs >= 0x2e80 && ucs <= 0xa4cf && (ucs & ~0x0011) != 0x300a &&
       ucs != 0x303f) ||                  /* CJK ... Yi */
      (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
      (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
      (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
      (ucs >= 0xff00 && ucs <= 0xff5f) || /* Fullwidth Forms */
      (ucs >= 0xffe0 && ucs <= 0xffe6) ||
      (ucs >= 0x20000 && ucs <= 0x2ffff)));
}


int wcswidth(const wchar_t *pwcs, size_t n)
{
  int w, width = 0;

  for (;*pwcs && n-- > 0; pwcs++)
    if ((w = wcwidth(*pwcs)) < 0)
      return -1;
    else
      width += w;

  return width;
}


/*
 * The following function is the same as wcwidth(), except that
 * spacing characters in the East Asian Ambiguous (A) category as
 * defined in Unicode Technical Report #11 have a column width of 2.
 * This experimental variant might be useful for users of CJK legacy
 * encodings who want to migrate to UCS. It is not otherwise
 * recommended for general use.
 */
static int wcwidth_cjk(wchar_t ucs)
{
  /* sorted list of non-overlapping intervals of East Asian Ambiguous
   * characters */
  static const struct interval ambiguous[] = {
    { 0x00A1, 0x00A1 }, { 0x00A4, 0x00A4 }, { 0x00A7, 0x00A8 },
    { 0x00AA, 0x00AA }, { 0x00AD, 0x00AE }, { 0x00B0, 0x00B4 },
    { 0x00B6, 0x00BA }, { 0x00BC, 0x00BF }, { 0x00C6, 0x00C6 },
    { 0x00D0, 0x00D0 }, { 0x00D7, 0x00D8 }, { 0x00DE, 0x00E1 },
    { 0x00E6, 0x00E6 }, { 0x00E8, 0x00EA }, { 0x00EC, 0x00ED },
    { 0x00F0, 0x00F0 }, { 0x00F2, 0x00F3 }, { 0x00F7, 0x00FA },
    { 0x00FC, 0x00FC }, { 0x00FE, 0x00FE }, { 0x0101, 0x0101 },
    { 0x0111, 0x0111 }, { 0x0113, 0x0113 }, { 0x011B, 0x011B },
    { 0x0126, 0x0127 }, { 0x012B, 0x012B }, { 0x0131, 0x0133 },
    { 0x0138, 0x0138 }, { 0x013F, 0x0142 }, { 0x0144, 0x0144 },
    { 0x0148, 0x014B }, { 0x014D, 0x014D }, { 0x0152, 0x0153 },
    { 0x0166, 0x0167 }, { 0x016B, 0x016B }, { 0x01CE, 0x01CE },
    { 0x01D0, 0x01D0 }, { 0x01D2, 0x01D2 }, { 0x01D4, 0x01D4 },
    { 0x01D6, 0x01D6 }, { 0x01D8, 0x01D8 }, { 0x01DA, 0x01DA },
    { 0x01DC, 0x01DC }, { 0x0251, 0x0251 }, { 0x0261, 0x0261 },
    { 0x02C4, 0x02C4 }, { 0x02C7, 0x02C7 }, { 0x02C9, 0x02CB },
    { 0x02CD, 0x02CD }, { 0x02D0, 0x02D0 }, { 0x02D8, 0x02DB },
    { 0x02DD, 0x02DD }, { 0x02DF, 0x02DF }, { 0x0300, 0x034E },
    { 0x0360, 0x0362 }, { 0x0391, 0x03A1 }, { 0x03A3, 0x03A9 },
    { 0x03B1, 0x03C1 }, { 0x03C3, 0x03C9 }, { 0x0401, 0x0401 },
    { 0x0410, 0x044F }, { 0x0451, 0x0451 }, { 0x2010, 0x2010 },
    { 0x2013, 0x2016 }, { 0x2018, 0x2019 }, { 0x201C, 0x201D },
    { 0x2020, 0x2022 }, { 0x2024, 0x2027 }, { 0x2030, 0x2030 },
    { 0x2032, 0x2033 }, { 0x2035, 0x2035 }, { 0x203B, 0x203B },
    { 0x203E, 0x203E }, { 0x2074, 0x2074 }, { 0x207F, 0x207F },
    { 0x2081, 0x2084 }, { 0x20AC, 0x20AC }, { 0x2103, 0x2103 },
    { 0x2105, 0x2105 }, { 0x2109, 0x2109 }, { 0x2113, 0x2113 },
    { 0x2116, 0x2116 }, { 0x2121, 0x2122 }, { 0x2126, 0x2126 },
    { 0x212B, 0x212B }, { 0x2153, 0x2155 }, { 0x215B, 0x215E },
    { 0x2160, 0x216B }, { 0x2170, 0x2179 }, { 0x2190, 0x2199 },
    { 0x21B8, 0x21B9 }, { 0x21D2, 0x21D2 }, { 0x21D4, 0x21D4 },
    { 0x21E7, 0x21E7 }, { 0x2200, 0x2200 }, { 0x2202, 0x2203 },
    { 0x2207, 0x2208 }, { 0x220B, 0x220B }, { 0x220F, 0x220F },
    { 0x2211, 0x2211 }, { 0x2215, 0x2215 }, { 0x221A, 0x221A },
    { 0x221D, 0x2220 }, { 0x2223, 0x2223 }, { 0x2225, 0x2225 },
    { 0x2227, 0x222C }, { 0x222E, 0x222E }, { 0x2234, 0x2237 },
    { 0x223C, 0x223D }, { 0x2248, 0x2248 }, { 0x224C, 0x224C },
    { 0x2252, 0x2252 }, { 0x2260, 0x2261 }, { 0x2264, 0x2267 },
    { 0x226A, 0x226B }, { 0x226E, 0x226F }, { 0x2282, 0x2283 },
    { 0x2286, 0x2287 }, { 0x2295, 0x2295 }, { 0x2299, 0x2299 },
    { 0x22A5, 0x22A5 }, { 0x22BF, 0x22BF }, { 0x2312, 0x2312 },
    { 0x2329, 0x232A }, { 0x2460, 0x24BF }, { 0x24D0, 0x24E9 },
    { 0x2500, 0x254B }, { 0x2550, 0x2574 }, { 0x2580, 0x258F },
    { 0x2592, 0x2595 }, { 0x25A0, 0x25A1 }, { 0x25A3, 0x25A9 },
    { 0x25B2, 0x25B3 }, { 0x25B6, 0x25B7 }, { 0x25BC, 0x25BD },
    { 0x25C0, 0x25C1 }, { 0x25C6, 0x25C8 }, { 0x25CB, 0x25CB },
    { 0x25CE, 0x25D1 }, { 0x25E2, 0x25E5 }, { 0x25EF, 0x25EF },
    { 0x2605, 0x2606 }, { 0x2609, 0x2609 }, { 0x260E, 0x260F },
    { 0x261C, 0x261C }, { 0x261E, 0x261E }, { 0x2640, 0x2640 },
    { 0x2642, 0x2642 }, { 0x2660, 0x2661 }, { 0x2663, 0x2665 },
    { 0x2667, 0x266A }, { 0x266C, 0x266D }, { 0x266F, 0x266F },
    { 0x273D, 0x273D }, { 0x3008, 0x300B }, { 0x3014, 0x3015 },
    { 0x3018, 0x301B }, { 0xFFFD, 0xFFFD }
  };

  /* binary search in table of non-spacing characters */
  if (bisearch(ucs, ambiguous,
	       sizeof(ambiguous) / sizeof(struct interval) - 1))
    return 2;

  return wcwidth(ucs);
}


int wcswidth_cjk(const wchar_t *pwcs, size_t n)
{
  int w, width = 0;

  for (;*pwcs && n-- > 0; pwcs++)
    if ((w = wcwidth_cjk(*pwcs)) < 0)
      return -1;
    else
      width += w;

  return width;
}
static int	 dangerous_locale(void);
static int	 grow_dst(char **, size_t *, size_t, char **, size_t);
static int	 vasnmprintf(char **, size_t, int *, const char *, va_list);


/*
 * For US-ASCII and UTF-8 encodings, we can safely recover from
 * encoding errors and from non-printable characters.  For any
 * other encodings, err to the side of caution and abort parsing:
 * For state-dependent encodings, recovery is impossible.
 * For arbitrary encodings, replacement of non-printable
 * characters would be non-trivial and too fragile.
 * The comments indicate what nl_langinfo(CODESET)
 * returns for US-ASCII on various operating systems.
 */

static int
dangerous_locale(void) {
	char	*loc;

	loc = "";//nl_langinfo(CODESET);
	return strcmp(loc, "UTF-8") != 0 &&
	    strcmp(loc, "US-ASCII") != 0 &&		/* OpenBSD */
	    strcmp(loc, "ANSI_X3.4-1968") != 0 &&	/* Linux */
	    strcmp(loc, "ISO8859-1") != 0 &&		/* AIX */
	    strcmp(loc, "646") != 0 &&			/* Solaris, NetBSD */
	    strcmp(loc, "") != 0;			/* Solaris 6 */
}

static int
grow_dst(char **dst, size_t *sz, size_t maxsz, char **dp, size_t need)
{
	char	*tp;
	size_t	 tsz;

	if (*dp + need < *dst + *sz)
		return 0;
	tsz = *sz + 128;
	if (tsz > maxsz)
		tsz = maxsz;
	if ((tp = recallocarray(*dst, *sz, tsz, 1)) == NULL)
		return -1;
	*dp = tp + (*dp - *dst);
	*dst = tp;
	*sz = tsz;
	return 0;
}

/*
 * The following two functions limit the number of bytes written,
 * including the terminating '\0', to sz.  Unless wp is NULL,
 * they limit the number of display columns occupied to *wp.
 * Whichever is reached first terminates the output string.
 * To stay close to the standard interfaces, they return the number of
 * non-NUL bytes that would have been written if both were unlimited.
 * If wp is NULL, newline, carriage return, and tab are allowed;
 * otherwise, the actual number of columns occupied by what was
 * written is returned in *wp.
 */

static int
vasnmprintf(char **str, size_t maxsz, int *wp, const char *fmt, va_list ap)
{
	char	*src;	/* Source string returned from vasprintf. */
	char	*sp;	/* Pointer into src. */
	char	*dst;	/* Destination string to be returned. */
	char	*dp;	/* Pointer into dst. */
	char	*tp;	/* Temporary pointer for dst. */
	size_t	 sz;	/* Number of bytes allocated for dst. */
	wchar_t	 wc;	/* Wide character at sp. */
	int	 len;	/* Number of bytes in the character at sp. */
	int	 ret;	/* Number of bytes needed to format src. */
	int	 width;	/* Display width of the character wc. */
	int	 total_width, max_width, print;

	src = NULL;
	if ((ret = vasprintf(&src, fmt, ap)) <= 0)
		goto fail;

	sz = strlen(src) + 1;
	if ((dst = malloc(sz)) == NULL) {
		free(src);
		ret = -1;
		goto fail;
	}

	if (maxsz > INT_MAX)
		maxsz = INT_MAX;

	sp = src;
	dp = dst;
	ret = 0;
	print = 1;
	total_width = 0;
	max_width = wp == NULL ? INT_MAX : *wp;
	while (*sp != '\0') {
		if ((len = mbtowc(&wc, sp, MB_CUR_MAX)) == -1) {
			(void)mbtowc(NULL, NULL, MB_CUR_MAX);
			if (dangerous_locale()) {
				ret = -1;
				break;
			}
			len = 1;
			width = -1;
		} else if (wp == NULL &&
		    (wc == L'\n' || wc == L'\r' || wc == L'\t')) {
			/*
			 * Don't use width uninitialized; the actual
			 * value doesn't matter because total_width
			 * is only returned for wp != NULL.
			 */
			width = 0;
		} else if ((width = wcwidth(wc)) == -1 &&
		    dangerous_locale()) {
			ret = -1;
			break;
		}

		/* Valid, printable character. */

		if (width >= 0) {
			if (print && (dp - dst >= (int)maxsz - len ||
			    total_width > max_width - width))
				print = 0;
			if (print) {
				if (grow_dst(&dst, &sz, maxsz,
				    &dp, len) == -1) {
					ret = -1;
					break;
				}
				total_width += width;
				memcpy(dp, sp, len);
				dp += len;
			}
			sp += len;
			if (ret >= 0)
				ret += len;
			continue;
		}

		/* Escaping required. */

		while (len > 0) {
			if (print && (dp - dst >= (int)maxsz - 4 ||
			    total_width > max_width - 4))
				print = 0;
			if (print) {
				if (grow_dst(&dst, &sz, maxsz,
				    &dp, 4) == -1) {
					ret = -1;
					break;
				}
				tp = vis(dp, *sp, VIS_OCTAL | VIS_ALL, 0);
				width = tp - dp;
				total_width += width;
				dp = tp;
			} else
				width = 4;
			len--;
			sp++;
			if (ret >= 0)
				ret += width;
		}
		if (len > 0)
			break;
	}
	free(src);
	*dp = '\0';
	*str = dst;
	if (wp != NULL)
		*wp = total_width;

	/*
	 * If the string was truncated by the width limit but
	 * would have fit into the size limit, the only sane way
	 * to report the problem is using the return value, such
	 * that the usual idiom "if (ret < 0 || ret >= sz) error"
	 * works as expected.
	 */

	if (ret < (int)maxsz && !print)
		ret = -1;
	return ret;

fail:
	if (wp != NULL)
		*wp = 0;
	if (ret == 0) {
		*str = src;
		return 0;
	} else {
		*str = NULL;
		return -1;
	}
}

int
snmprintf(char *str, size_t sz, int *wp, const char *fmt, ...)
{
	va_list	 ap;
	char	*cp;
	int	 ret;

	va_start(ap, fmt);
	ret = vasnmprintf(&cp, sz, wp, fmt, ap);
	va_end(ap);
	if (cp != NULL) {
		(void)strlcpy(str, cp, sz);
		free(cp);
	} else
		*str = '\0';
	return ret;
}

/*
 * To stay close to the standard interfaces, the following functions
 * return the number of non-NUL bytes written.
 */

int
vfmprintf(FILE *stream, const char *fmt, va_list ap)
{
	char	*str;
	int	 ret;

	if ((ret = vasnmprintf(&str, INT_MAX, NULL, fmt, ap)) < 0)
		return -1;
	if (fputs(str, stream) == EOF)
		ret = -1;
	free(str);
	return ret;
}

int
fmprintf(FILE *stream, const char *fmt, ...)
{
	va_list	 ap;
	int	 ret;

	va_start(ap, fmt);
	ret = vfmprintf(stream, fmt, ap);
	va_end(ap);
	return ret;
}

int
mprintf(const char *fmt, ...)
{
	va_list	 ap;
	int	 ret;

	va_start(ap, fmt);
	ret = vfmprintf(stdout, fmt, ap);
	va_end(ap);
	return ret;
}

/*
 * Set up libc for multibyte output in the user's chosen locale.
 *
 * XXX: we are known to have problems with Turkish (i/I confusion) so we
 *      deliberately fall back to the C locale for now. Longer term we should
 *      always prefer to select C.[encoding] if possible, but there's no
 *      standardisation in locales between systems, so we'll need to survey
 *      what's out there first.
 */
void
msetlocale(void)
{
	const char *vars[] = { "LC_ALL", "LC_CTYPE", "LANG", NULL };
	char *cp;
	int i;

	/*
	 * We can't yet cope with dotless/dotted I in Turkish locales,
	 * so fall back to the C locale for these.
	 */
	for (i = 0; vars[i] != NULL; i++) {
		if ((cp = getenv(vars[i])) == NULL)
			continue;
		if (strncasecmp(cp, "TR", 2) != 0)
			break;
		/*
		 * If we're in a UTF-8 locale then prefer to use
		 * the C.UTF-8 locale (or equivalent) if it exists.
		 */
		if ((strcasestr(cp, "UTF-8") != NULL ||
		    strcasestr(cp, "UTF8") != NULL) &&
		    (setlocale(LC_CTYPE, "C.UTF-8") != NULL ||
		    setlocale(LC_CTYPE, "POSIX.UTF-8") != NULL))
			return;
		setlocale(LC_CTYPE, "C");
		return;
	}
	/* We can handle this locale */
	setlocale(LC_CTYPE, "");
}
