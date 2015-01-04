## Process this file with automake to produce Makefile.in.
SUBDIRS = libparrillada-utils libparrillada-media libparrillada-burn plugins src po data

if BUILD_CAJA
SUBDIRS += caja
endif

# Automake 1.12 should make this unnecessary.
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

parrilladadocdir = ${prefix}/docs/parrillada

EXTRA_DIST = 			\
	AUTHORS			\
	MAINTAINERS		\
	NEWS			\
	README			\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	mate-doc-utils.make

noinst_PROGRAMS =

DISTCHECK_CONFIGURE_FLAGS =		\
	--disable-scrollkeeper		\
	--enable-gtk-doc			\
	--with-cajadir='$${libdir}/caja/extensions-2.0-distcheck'	\
	--with-girdir='$${datadir}/girdir-distcheck/'						\
	--with-typelibdir='$${libdir}/typelibdir-distcheck'

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update	\
	libparrillada-media@PARRILLADA_LIBRARY_SUFFIX@.pc	\
	libparrillada-burn@PARRILLADA_LIBRARY_SUFFIX@.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libparrillada-media@PARRILLADA_LIBRARY_SUFFIX@.pc libparrillada-burn@PARRILLADA_LIBRARY_SUFFIX@.pc

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generated by Makefile. Do not edit.'; echo; \
		  $(top_srcdir)/missing --run git log --stat ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
			echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog >&2; \
	fi

-include $(top_srcdir)/git.mk