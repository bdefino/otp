/*
Copyright (C) 2020 Bailey Defino
<https://bdefino.github.io>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* apply a one time pad */

#undef BUFLEN
#undef OPTIONS
#undef OPTSTRING
#undef USAGE

#define BUFLEN 512 /* 512B */
#define OPTIONS ("KEY\n" \
  "\tthe key file to XOR with TARGET\n" \
  "OPTIONS\n" \
  "\t-0 INT\n" \
  "\t\tseek to INT before writing the output\n" \
  "\t-b INT\n" \
  "\t\tset the buffer length manually\n" \
  "\t\t(for security, keep this small; defaults to 512B)\n" \
  "\t-c INT\n" \
  "\t\tthe count for which to apply\n" \
  "\t-h\n" \
  "\t\tprint this text and exit\n" \
  "\t-k INT\n" \
  "\t\tseek to INT before reading the KEY\n" \
  "\t-o PATH\n" \
  "\t\tchange the output PATH\n" \
  "\t\t(setting this to an alternate path increases the number of copies\n" \
  "\t\tof the output, so use it sparingly; defaults to TARGET)\n" \
  "\t-t INT\n" \
  "\t\tseek to INT before reading the TARGET\n" \
  "TARGET\n" \
  "\tthe input file to modify,\n" \
  "\tand (unless `-o` is specified), the output location\n")
#define OPTSTRING "b:c:hk:o:0:t:+"
#define USAGE ("apply a one time pad\n" \
  "Usage: %s [OPTIONS] KEY TARGET\n")

/* `memset` that won't be optimized out by the compiler */
static void *(* volatile memshred)(void *, int, size_t) = \
  (void *(* volatile)(void *, int, size_t)) &memset;

/* print the usage */
static void usage(const char *executable);

/* print the usage and option information */
static void help(const char *executable) {
  usage(executable != NULL ? executable : "?");
  fprintf(stderr, OPTIONS);
}

/* XOR a key file with an input file to an output file */
int otp(const int ofd, const int ifd, const int kfd, const size_t buflen,
  off_t lim);

int main(int argc, char **argv) {
  size_t buflen;
  int _errno;
  int has_ocount;
  int kfd;
  off_t koffset;
  char *kpath;
  off_t ocount;
  int ofd;
  off_t ooffset;
  char *opath;
  int opt;
  struct stat ost;
  char *perrors;
  int retval;
  int tfd;
  off_t toffset;
  char *tpath;

  buflen = BUFLEN;
  has_ocount = 0;
  kfd = -1;
  koffset = 0;
  kpath = NULL;
  ofd = -1;
  opath = NULL;
  perrors = NULL;
  retval = EXIT_SUCCESS;
  tfd = -1;
  toffset = 0;
  tpath = NULL;

  while (1) {
    opt = getopt(argc, argv, OPTSTRING);

    if (opt == -1) {
      break;
    }

    switch (opt) {
      case '0':
        ooffset = atoi(optarg);
        break;
      case 'b':
        buflen = atoi(optarg);
        buflen = buflen > 0 ? buflen : BUFLEN;
        break;
      case 'c':
        ocount = atoi(optarg);
        has_ocount = ocount >= 0 ? 1 : 0;
        break;
      case 'h':
        help(argv[0]);
        goto bubble;
      case 'k':
        koffset = atoi(optarg);
        break;
      case 'o':
        opath = optarg;
        break;
      case 't':
        toffset = atoi(optarg);
        break;

      /* fall-through */

      case ':':
        fprintf(stderr, "`%s` expected an argument.\n", optarg);
      case '?':
        fprintf(stderr, "`%s` isn't an option.\n", optarg);
      default:
        help(argv[0]);
        retval = EXIT_FAILURE;
        goto bubble;
    }
  }

  if (optind >= argc
      || argc - optind < 2) {
    usage(argv[0]);
    retval = EXIT_FAILURE;
    goto bubble;
  }
  kpath = argv[optind];
  tpath = argv[optind + 1];
  opath = opath == NULL ? tpath : opath;

  /* open files */

  kfd = open(kpath, O_RDONLY);

  if (kfd < 0) {
    perrors = kpath;
    retval = -errno;
    goto bubble;
  }
  tfd = open(tpath, O_RDONLY);

  if (tfd < 0) {
    perrors = tpath;
    retval = -errno;
    goto bubble;
  }
  ofd = open(opath, O_CREAT | O_RDONLY | O_WRONLY);

  if (ofd < 0) {
    perrors = opath;
    retval = -errno;
    goto bubble;
  }

  /* offset files */
  
  if (lseek(kfd, koffset, SEEK_SET) < 0) {
    perrors = kpath;
    retval = -errno;
    goto bubble;
  }

  if (lseek(tfd, toffset, SEEK_SET) < 0) {
    perrors = tpath;
    retval = -errno;
    goto bubble;
  }

  if (lseek(ofd, ooffset, SEEK_SET) < 0) {
    perrors = opath;
    retval = -errno;
    goto bubble;
  }

  if (!has_ocount) {
    /* set the count to the target's size */

    if (stat(tpath, &ost)) {
      perrors = tpath;
      retval = -errno;
      goto bubble;
    }
    ocount = ost.st_size;
  }

  /* apply the one time pad */

  retval = otp(ofd, tfd, kfd, buflen, ocount);

bubble:

  _errno = errno;

  if (ofd >= 0) {
    close(ofd);
  }

  if (tfd >= 0) {
    close(tfd);
  }

  if (kfd >= 0) {
    close(kfd);
  }
  errno = _errno;
  
  if (retval) {
    perror(perrors);
  }
  return retval;
}

int otp(const int ofd, int ifd, const int kfd, const size_t buflen, off_t lim) {
  size_t cbuflen;
  size_t i;
  char ibuf[buflen];
  char *ibufp;
  ssize_t icbuflen;
  ssize_t irbuflen;
  char obuf[buflen];
  char *obufp;
  ssize_t ocbuflen;
  ssize_t owbuflen;
  int retval;

  retval = 0;

  if (!buflen) {
    retval = -EINVAL;
    goto bubble;
  }

  if (ifd < 0) {
    retval  = -EBADF;
    goto bubble;
  }

  if (kfd < 0) {
    retval = -EBADF;
    goto bubble;
  }

  if (ofd < 0) {
    retval = -EBADF;
    goto bubble;
  }

  while (lim > 0) {
    /* (partially) fill the buffer with the key */

    cbuflen = ocbuflen = read(kfd, obuf, buflen < lim ? buflen : lim);

    if (ocbuflen < 0) {
      retval = -errno;
      goto bubble;
    } else if (!ocbuflen) {
      /* EOF */

      retval = -EIO;
      goto bubble;
    }
    lim -= cbuflen;

    /* read the corresponding input */

    for (ibufp = ibuf, icbuflen = 0; icbuflen < ocbuflen; ) {
      irbuflen = read(ifd, ibufp, ocbuflen - icbuflen);

      if (irbuflen < 0) {
        retval = -errno;
        goto bubble;
      } else if (!irbuflen) {
        /* EOF */

        retval = -EIO;
        goto bubble;
      }
      ibufp += irbuflen;
      icbuflen += irbuflen;
    }

    /* XOR */

    for (i = 0; i < cbuflen; i++) {
      obuf[i] ^= ibuf[i];
    }

    /* shred input ASAP */

    memshred(ibuf, '\0', cbuflen);

    /* write the output */

    for (obufp = obuf; ocbuflen > 0; ) {
      owbuflen = write(ofd, obuf, ocbuflen);

      if (owbuflen < 0) {
        retval = -errno;
        goto bubble;
      }
      obufp += owbuflen;
      ocbuflen -= icbuflen;
    }

    /* shred output ASAP */

    memshred(obuf, '\0', cbuflen);
  }

bubble:

  if (ofd >= 0) {
    if (fdatasync(ofd)
        && !retval) {
      retval = -errno;
    }
  }
  memshred(ibuf, '\0', cbuflen);
  memshred(obuf, '\0', cbuflen);
  return retval;
}

static void usage(const char *executable) {
  fprintf(stderr, USAGE, executable);
}

