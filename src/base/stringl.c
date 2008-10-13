/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND TODD C. MILLER DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL TODD C. MILLER BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#ifndef HAVE_STRLCAT

#include <sys/types.h>
#include <assert.h>
#include <string.h>

/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat (char *dst, const char *src, size_t siz)
{
  char *d = dst;
  const char *s = src;
  size_t n = siz;
  size_t dlen;

  assert (dst != NULL);
  assert (src != NULL);

  /* Find the end of dst and adjust bytes left but don't go past end */
  while (n-- != 0 && *d != '\0')
    d++;
  dlen = d - dst;
  n = siz - dlen;

  if (n == 0)
    return (dlen + strlen (s));
  while (*s != '\0')
    {
      if (n != 1)
	{
	  *d++ = *s;
	  n--;
	}
      s++;
    }
  *d = '\0';

  return (dlen + (s - src));	/* count does not include NUL */
}
#endif /* !HAVE_STRLCAT */

#ifndef HAVE_STRLCPY

#include <sys/types.h>
#include <assert.h>
#include <string.h>
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy (char *dst, const char *src, size_t siz)
{
  char *d = dst;
  const char *s = src;
  size_t n = siz;

  assert (dst != NULL);
  assert (src != NULL);

  /* Copy as many bytes as will fit */
  if (n != 0 && --n != 0)
    {
      do
	{
	  if ((*d++ = *s++) == 0)
	    break;
	}
      while (--n != 0);
    }

  /* Not enough room in dst, add NUL and traverse rest of src */
  if (n == 0)
    {
      if (siz != 0)
	*d = '\0';		/* NUL-terminate dst */
      while (*s++)
	;
    }

  return (s - src - 1);		/* count does not include NUL */
}
#endif /* !HAVE_STRLCPY */
