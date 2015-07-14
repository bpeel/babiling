AC_PREREQ(2.59)

AC_INIT(finvenkisto, [0.1])
AC_CONFIG_SRCDIR(client/fv-main.c)
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)
AC_CANONICAL_HOST
AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.11 foreign -Wno-portability no-define no-dist-gzip
                  dist-bzip2 tar-ustar])
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CPP
AM_PROG_AR
AC_PROG_RANLIB
AM_PROG_CC_C_O
AC_ISC_POSIX
AC_C_CONST

FINVENKISTO_EXTRA_CFLAGS=""
FINVENKISTO_EXTRA_LIBS=""
SERVER_EXTRA_CFLAGS=""
SERVER_EXTRA_LIBS=""

AC_CHECK_DECL([EMSCRIPTEN], [IS_EMSCRIPTEN=yes], [IS_EMSCRIPTEN=no])
AM_CONDITIONAL([IS_EMSCRIPTEN], [test "x$IS_EMSCRIPTEN" = xyes])

AC_C_BIGENDIAN([AC_DEFINE([HAVE_BIG_ENDIAN], [1], [System is big-endian])],
               [AC_DEFINE([HAVE_LITTLE_ENDIAN], [1],
                          [System is little-endian])])

AC_CHECK_SIZEOF([unsigned long])

AC_CHECK_LIB([m], sinf)
AC_CHECK_FUNCS([ffs ffsl])

have_alignof=no
AC_MSG_CHECKING([for alignof])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <stdalign.h>
                                    const int thing = alignof (int);],
                                   [(void) 0])],
                  [have_alignof=yes
                   AC_DEFINE([HAVE_ALIGNOF], 1,
                             [Defined if alignof is available])
                   AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])])
AC_MSG_CHECKING([for __alignof__])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([const int t = __alignof__ (int);],
                                   [(void) 0])],
                  [have_alignof=yes
                   AC_DEFINE([HAVE_UUALIGNOF], 1,
                             [Defined if __alignof__ is available])
                   AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])])
AS_IF([test "x$have_alignof" = "xno"],
      [AC_MSG_ERROR([No operator for alignof found])])

have_sync_ref_count=yes
AC_MSG_CHECKING([for __sync_fetch_and_add])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([int x;],
                                   [__sync_fetch_and_add(&x, 1);])],
                  [AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])
                   have_sync_ref_count=no])
AC_MSG_CHECKING([for __sync_fetch_and_sub])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([int x;],
                                   [__sync_fetch_and_sub(&x, 1);])],
                  [AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])
                   have_sync_ref_count=no])
AS_IF([test "x$have_sync_ref_count" = "xyes"],
      [AC_DEFINE([HAVE_SYNC_REF_COUNT], [1],
                 [GCC extensions for atomic ref-counting are available])])

AC_MSG_CHECKING([for _Static_assert])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([_Static_assert (1, "");],
                                   [(void) 0])],
                  [AC_DEFINE([HAVE_STATIC_ASSERT], [1],
                             [Whether _Static_assert can be used or not])
                   AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])])

have_clock_gettime=no
AC_CHECK_FUNC([clock_gettime], [have_clock_gettime=yes])
AS_IF([test "x$have_clock_gettime" = "xno"],
      [AC_CHECK_LIB([rt], [clock_gettime],
                    [SERVER_EXTRA_LIBS="$SERVER_EXTRA_LIBS -lrt"],
                    [clock_gettime required but not found])])

AC_CHECK_FUNCS([getpeereid])

dnl     ============================================================
dnl     Enable strict compiler flags
dnl     ============================================================

m4_define([maintainer_default], [yes])
AC_ARG_ENABLE(
  [maintainer-flags],
  [AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes/error@:>@],
                  [Use strict compiler flags @<:@default=]maintainer_default[@:>@])],
  [],
  enable_maintainer_flags=maintainer_default
)

MAINTAINER_COMPILER_FLAGS="-Wall -Wuninitialized -Wempty-body -Wformat
                           -Wformat-security -Winit-self -Wundef
                           -Wdeclaration-after-statement -Wvla
                           -Wpointer-arith -Wmissing-declarations"

AS_CASE(
  [$enable_maintainer_flags],
  [yes],
  [
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [no],
  [
  ],
  [error],
  [
    MAINTAINER_COMPILER_FLAGS="$MAINTAINER_COMPILER_FLAGS -Werror"
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [*],
  [AC_MSG_ERROR([Invalid option for --enable-maintainer-flags])]
)

# strip leading spaces
FINVENKISTO_EXTRA_CFLAGS="$FINVENKISTO_EXTRA_CFLAGS ${MAINTAINER_CFLAGS#*  }"

AS_COMPILER_FLAG([-std=c99],
         [FINVENKISTO_EXTRA_CFLAGS="-std=c99 $FINVENKISTO_EXTRA_CFLAGS"])

PKG_CHECK_MODULES([OPUS], [opus])

AS_IF([test "x$IS_EMSCRIPTEN" = "xyes"],
      [SDL_CFLAGS="-s USE_SDL=2"
       AC_SUBST([SDL_CFLAGS])],
      [AS_CASE([$host_os],
               [mingw*],
               [GL_CFLAGS=""],
               [PKG_CHECK_MODULES([GL], [gl])])
      PKG_CHECK_MODULES([SDL], [sdl2])])

AC_SUBST(FINVENKISTO_EXTRA_CFLAGS)
AC_SUBST(FINVENKISTO_EXTRA_LIBS)

AC_OUTPUT(
Makefile
client/data/Makefile
client/rply/Makefile
client/Makefile
common/Makefile
doc/Makefile
server/Makefile
)
