dnl Process this file with autoconf to produce a configure script.
AC_INIT([parrillada],[1.18.3])
AC_CONFIG_SRCDIR(src/main.c)

PARRILLADA_MAJOR_VERSION=1
PARRILLADA_MINOR_VERSION=18
PARRILLADA_SUB=2
PARRILLADA_VERSION=$PARRILLADA_MAJOR_VERSION.$PARRILLADA_MINOR_VERSION.$PARRILLADA_SUB

AC_SUBST(PARRILLADA_MAJOR_VERSION)
AC_SUBST(PARRILLADA_MINOR_VERSION)
AC_SUBST(PARRILLADA_SUB)
AC_SUBST(PARRILLADA_VERSION)

dnl Before making a release, the BURN_LT_VERSION string should be modified.
dnl The string is of the form C:R:A.
dnl - If interfaces have been changed or added, but binary compatibility has
dnl   been preserved, change to C+1:0:A+1
dnl - If binary compatibility has been broken (eg removed or changed interfaces)
dnl   change to C+1:0:0
dnl - If the interface is the same as the previous version, change to C:R+1:A

LT_CURRENT=3
LT_REVISION=6
LT_AGE=2

AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

LIBPARRILLADA_LT_VERSION=$LT_CURRENT:$LT_REVISION:$LT_AGE
AC_SUBST(LIBPARRILLADA_LT_VERSION) 

TYPELIB_MIN_VERSION=$((LT_CURRENT-LT_AGE))
AC_SUBST(TYPELIB_MIN_VERSION)

AC_DEFINE_UNQUOTED(PARRILLADA_MAJOR_VERSION, $PARRILLADA_MAJOR_VERSION, [major version])
AC_DEFINE_UNQUOTED(PARRILLADA_MINOR_VERSION, $PARRILLADA_MINOR_VERSION, [minor version])
AC_DEFINE_UNQUOTED(PARRILLADA_SUB, $PARRILLADA_SUB, [sub version])
AC_DEFINE_UNQUOTED(PARRILLADA_VERSION, $PARRILLADA_VERSION, [version])

dnl this is what it used to be
dnl AM_INIT_AUTOMAKE(parrillada, $PARRILLADA_VERSION)

AM_INIT_AUTOMAKE([foreign])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_CONFIG_HEADERS([config.h])

AC_DISABLE_STATIC
LT_INIT

AM_MAINTAINER_MODE

dnl **************** Support GSettings *************************
GLIB_GSETTINGS

dnl **************** documentation *****************************
YELP_HELP_INIT
GTK_DOC_CHECK([1.12])

dnl ***************** MATE MACROS *****************************

dnl This allows to set warnings
MATE_COMPILE_WARNINGS(yes)

dnl This could be use one day not to compile all debugging message
dnl MATE_DEBUG_CHECK

MATE_MAINTAINER_MODE_DEFINES
DISABLE_DEPRECATED="$DISABLE_DEPRECATED -DGSEAL_ENABLE"
AC_SUBST(DISABLE_DEPRECATED)

dnl ***************** GENERAL **********************************

dnl ** reminder: the following set CFLAGS to -O2 -g if empty
AC_PROG_CC
AC_HEADER_STDC

dnl Set PACKAGE_DATA_DIR in config.h.
if test "x${datadir}" = 'x${prefix}/share'; then
  if test "x${prefix}" = "xNONE"; then
    AC_DEFINE_UNQUOTED([PACKAGE_DATA_DIR], "${ac_default_prefix}/share/", [Define the PACKAGE_DATA_DIR.])
  else
    AC_DEFINE_UNQUOTED([PACKAGE_DATA_DIR], "${prefix}/share/", [Define the PACKAGE_DATA_DIR.])
  fi
  elif test "x${datadir}" = 'x${datarootdir}'; then
  if test "x${datarootdir}" = 'x${prefix}/share'; then
  	if test "x${prefix}" = "xNONE"; then
 	AC_DEFINE_UNQUOTED([PACKAGE_DATA_DIR], "${ac_default_prefix}/share/", [Define the PACKAGE_DATA_DIR.])
 	else
 	AC_DEFINE_UNQUOTED([PACKAGE_DATA_DIR], "${prefix}/share/", [Define the PACKAGE_DATA_DIR.])
 	fi
  else
 	AC_DEFINE_UNQUOTED([PACKAGE_DATA_DIR], "${datarootdir}/", [Define the PACKAGE_DATA_DIR.])
 	fi
  else
	AC_DEFINE_UNQUOTED([PACKAGE_DATA_DIR], "${datadir}/", [Define the PACKAGE_DATA_DIR.])
fi

dnl ***************** SCSI related *****************************
AC_SUBST(PARRILLADA_SCSI_LIBS)
AC_CHECK_HEADERS([camlib.h],[has_cam="yes"],[has_cam="no"])

dnl ***************** check for linux sg interface *************
AC_CHECK_TYPES([sg_io_hdr_t],[has_sg="yes"],[has_sg="no"],
[#include <sys/types.h>
 #include <scsi/sg.h>])

dnl ***************** check for netbsd *************************
AC_CHECK_TYPES([scsireq_t],[has_scsiio="yes"],[has_scsiio="no"],
[#include <sys/types.h>
 #include <sys/scsiio.h>])

dnl ***************** check for solaris uscsi interface ********
AC_CHECK_TYPES([struct uscsi_cmd],[has_uscsi="yes"],[has_uscsi="no"],
[#include <sys/types.h>
 #include <sys/scsi/impl/uscsi.h>])

if test x"$has_cam" = x"yes"; then
    PARRILLADA_SCSI_LIBS="-lcam"
elif test x"$has_sg" = x"yes"; then
	:
elif test x"$has_scsiio" = x"yes"; then
	:
elif test x"$has_uscsi" = x"yes"; then
	:
else
	AC_MSG_ERROR([Support Linux SG, FreeBSD CAM, NetBSD SCSIPI, Solaris USCSI. No supported SCSI interface headers could not be found.])
fi

AM_CONDITIONAL(HAVE_CAM_LIB_H, test x"$has_cam" = "xyes")
AM_CONDITIONAL(HAVE_SG_IO_HDR_T, test x"$has_sg" = "xyes")
AM_CONDITIONAL(HAVE_USCSI_H, test x"$has_uscsi" = "xyes")
AM_CONDITIONAL(HAVE_SCSIIO_H, test x"$has_scsiio" = "xyes")

dnl ***************** LARGE FILE SUPPORT ***********************

AC_SYS_LARGEFILE

dnl ********** Required libraries **********************

GLIB_REQUIRED=2.29.14
GTHREAD_REQUIRED=2.6.0
GMODULE_REQUIRED=2.6.0
GMODULE_EXPORT_REQUIRED=2.6.0
GIO_REQUIRED=2.28.0
GSTREAMER_REQUIRED=0.11.92
GSTREAMER_BASE_REQUIRED=0.11.92
GSTREAMER_MODULE_REQUIRED=0.11.92
LIBXML2_REQUIRED=2.6.0
MATE_DESKTOP_REQUIRED=1.17.0

dnl ** used by parrillada and one plugin
PKG_CHECK_MODULES(PARRILLADA_GSTREAMER, 			\
	gstreamer-1.0 >= $GSTREAMER_REQUIRED		\
	gstreamer-base-1.0 >= $GSTREAMER_REQUIRED		\
	gstreamer-video-1.0 >= $GSTREAMER_BASE_REQUIRED	\
	gstreamer-pbutils-1.0 >= $GSTREAMER_BASE_REQUIRED \
	gstreamer-tag-1.0 >= $GSTREAMER_BASE_REQUIRED)
PARRILLADA_GSTREAMER_CFLAGS="-DGST_USE_UNSTABLE_API $PARRILLADA_GSTREAMER_CFLAGS"
AC_SUBST(PARRILLADA_GSTREAMER_CFLAGS)
AC_SUBST(PARRILLADA_GSTREAMER_LIBS)

PARRILLADA_GSTREAMER_LIBS="$PARRILLADA_GSTREAMER_LIBS -lgstpbutils-1.0"

dnl ** used by parrillada and one plugin
PKG_CHECK_MODULES(PARRILLADA_GIO, 	\
	gio-2.0 >= $GIO_REQUIRED)

AC_SUBST(PARRILLADA_GIO_CFLAGS)
AC_SUBST(PARRILLADA_GIO_LIBS)

dnl ** used by parrillada and one plugin
PKG_CHECK_MODULES(PARRILLADA_LIBXML, 	\
	libxml-2.0 >= $LIBXML2_REQUIRED)

AC_SUBST(PARRILLADA_LIBXML_CFLAGS)
AC_SUBST(PARRILLADA_LIBXML_LIBS)

dnl ** used by parrillada and both libraries
PKG_CHECK_MODULES(PARRILLADA_GTHREAD, 		\
	gthread-2.0 >= $GTHREAD_REQUIRED)

AC_SUBST(PARRILLADA_GTHREAD_CFLAGS)
AC_SUBST(PARRILLADA_GTHREAD_LIBS)

dnl ** used by parrillada and all modules
PKG_CHECK_MODULES(PARRILLADA_GLIB, 		\
	gobject-2.0 >= $GLIB_REQUIRED		\
	glib-2.0 >= $GLIB_REQUIRED)

AC_SUBST(PARRILLADA_GLIB_CFLAGS)
AC_SUBST(PARRILLADA_GLIB_LIBS)

PKG_CHECK_MODULES(PARRILLADA_GMODULE,		\
	gmodule-2.0 >= $GMODULE_REQUIRED)

AC_SUBST(PARRILLADA_GMODULE_CFLAGS)
AC_SUBST(PARRILLADA_GMODULE_LIBS)

PKG_CHECK_MODULES(PARRILLADA_GMODULE_EXPORT,		\
	gmodule-export-2.0 >= $GMODULE_EXPORT_REQUIRED)

AC_SUBST(PARRILLADA_GMODULE_EXPORT_CFLAGS)
AC_SUBST(PARRILLADA_GMODULE_EXPORT_LIBS)

PKG_CHECK_MODULES(PARRILLADA_SM,
	sm \
	ice)

AC_SUBST(PARRILLADA_SM_CFLAGS)
AC_SUBST(PARRILLADA_SM_LIBS)

AC_CHECK_LIBM
AC_SUBST(LIBM)

#
# GTK+ stuff (taken and adapted from empathy)
#

GTK_REQUIRED=3.0.0

PKG_CHECK_MODULES([PARRILLADA_GTK],	\
		  gtk+-3.0 >= $GTK_REQUIRED	\
		  gdk-x11-3.0)

AC_SUBST(PARRILLADA_GTK_CFLAGS)
AC_SUBST(PARRILLADA_GTK_LIBS)

#
# MATE Desktop - Required to retrive the icon associated to the resource MIME
#

PKG_CHECK_MODULES(PARRILLADA_MATE_DESKTOP,
	      mate-desktop-2.0 >= $MATE_DESKTOP_REQUIRED)

AC_SUBST(PARRILLADA_MATE_DESKTOP_CFLAGS)
AC_SUBST(PARRILLADA_MATE_DESKTOP_LIBS)

#
# libnotify check
#

LIBNOTIFY_REQUIRED=0.6.1

PKG_CHECK_MODULES([PARRILLADA_LIBNOTIFY],[libnotify >= $LIBNOTIFY_REQUIRED])
AC_SUBST(PARRILLADA_LIBNOTIFY_CFLAGS)
AC_SUBST(PARRILLADA_LIBNOTIFY_LIBS)

#
# Canberra
#

CANBERRA_REQUIRED=0.1

PKG_CHECK_MODULES(PARRILLADA_CANBERRA,
	      libcanberra >= $CANBERRA_REQUIRED
	      libcanberra-gtk3 >= $CANBERRA_REQUIRED)

AC_SUBST(PARRILLADA_CANBERRA_CFLAGS)
AC_SUBST(PARRILLADA_CANBERRA_LIBS)

dnl ****************Caja**********************************
# Note: the extension is built against gtk+-3.0 so we cannot build extension with gtk+-2.0
CAJA_REQUIRED=1.17.0

AC_ARG_ENABLE(caja,
			AS_HELP_STRING([--enable-caja],[Build Caja extension [[default=yes]]]),
			[enable_caja=$enableval],
			[enable_caja="yes"])

AC_ARG_WITH(cajadir,
			AS_HELP_STRING([--with-cajadir=DIR],[Installation path for Caja extension @<:@auto@:>@]),
			[ac_with_cajadir=$withval],
			[ac_with_cajadir=""])

if test x"$enable_caja" = "xyes"; then
    	PKG_CHECK_MODULES(CAJA_EXTENSION,
			  libcaja-extension >= $CAJA_REQUIRED,
			  build_caja=yes,
			  build_caja=no)
else
	build_caja="no"
fi

if test x"$build_caja" = "xyes"; then
	CAJA_EXTENSION_CFLAGS="$PARRILLADA_GLIB_CFLAGS $PARRILLADA_UI_CFLAGS $CAJA_EXTENSION_CFLAGS"
	CAJA_EXTENSION_LIBS="$PARRILLADA_GLIB_LIBS $PARRILLADA_UI_LIBS $CAJA_EXTENSION_LIBS"

	if test "${ac_with_cajadir}" = ""; then
		ac_with_cajadir=`pkg-config --variable=extensiondir libcaja-extension`
	fi

	AC_MSG_NOTICE([installing plugin in ${ac_with_cajadir}])
	AC_SUBST([CAJADIR],[${ac_with_cajadir}])

	AM_GLIB_DEFINE_LOCALEDIR([GNOMELOCALEDIR])
fi

AM_CONDITIONAL(BUILD_CAJA, test x"$build_caja" = "xyes")

#
# Installation path for plugins
#

PARRILLADA_PLUGIN_DIRECTORY=${PARRILLADA_PLUGIN_DIRECTORY:-'${libdir}/parrillada3/plugins'}
AC_SUBST(PARRILLADA_PLUGIN_DIRECTORY)

dnl ****************check for libburn (optional)**************
LIBBURN_REQUIRED=0.4.0
LIBISOFS_REQUIRED=0.6.4

AC_ARG_ENABLE(libburnia,
			AS_HELP_STRING([--enable-libburnia],[Build libburnia plugins [[default=yes]]]),
			[enable_libburnia=$enableval],
			[enable_libburnia="yes"])

if test x"$enable_libburnia" = "xyes"; then
	PKG_CHECK_MODULES(LIBBURNIA, libburn-1 >= $LIBBURN_REQUIRED  libisofs-1 >= $LIBISOFS_REQUIRED, build_libburnia=yes, build_libburnia=no)
else
	build_libburnia="no"
fi

if test x"$build_libburnia" = "xyes"; then
	AC_SUBST(PARRILLADA_LIBBURNIA_CFLAGS)
	AC_SUBST(PARRILLADA_LIBBURNIA_LIBS)

	PARRILLADA_LIBBURNIA_CFLAGS="$LIBBURNIA_CFLAGS"
	PARRILLADA_LIBBURNIA_LIBS="$LIBBURNIA_LIBS"
	AC_DEFINE(BUILD_LIBBURNIA, 1, [define if you  want to build libburnia plugins])
fi
AM_CONDITIONAL(BUILD_LIBBURNIA, test x"$build_libburnia" = "xyes")

dnl **************** check for cdrtools **********************
AC_ARG_ENABLE(cdrtools,
			AS_HELP_STRING([--enable-cdrtools],[Build cdrtools plugins [[default=yes]]]),
			[build_cdrtools=$enableval],
			[build_cdrtools="yes"])

if test x"$build_cdrtools" = "xyes"; then
	AC_DEFINE(BUILD_CDRTOOLS, 1, [define if you  want to build cdrtools plugins])
else
	build_cdrtools="no"
fi
AM_CONDITIONAL(BUILD_CDRTOOLS, test x"$build_cdrtools" = "xyes")

dnl **************** check for cdrdao **********************
AC_ARG_ENABLE(cdrdao,
			AS_HELP_STRING([--enable-cdrdao],[Build cdrdao plugins [[default=yes]]]),
			[build_cdrdao=$enableval],
			[build_cdrdao="yes"])

if test x"$build_cdrdao" = "xyes"; then
	AC_DEFINE(BUILD_CDRDAO, 1, [define if you  want to build cdrdao plugins])
else
	build_cdrdao="no"
fi
AM_CONDITIONAL(BUILD_CDRDAO, test x"$build_cdrdao" = "xyes")

dnl **************** check for cdrkit ************************
AC_ARG_ENABLE(cdrkit,
			AS_HELP_STRING([--enable-cdrkit],[Build cdrkit plugins [[default=yes]]]),
			[build_cdrkit=$enableval],
			[build_cdrkit="yes"])

if test x"$build_cdrkit" = "xyes"; then
	AC_DEFINE(BUILD_CDRKIT, 1, [define if you  want to build cdrkit plugins])
else
	build_cdrkit="no"
fi
AM_CONDITIONAL(BUILD_CDRKIT, test x"$build_cdrkit" = "xyes")

dnl **************** check for growisofs ************************
AC_ARG_ENABLE(growisofs,
			AS_HELP_STRING([--enable-growisofs],[Build growisofs plugins [[default=yes]]]),
			[build_growisofs=$enableval],
			[build_growisofs="yes"])

if test x"$build_growisofs" = "xyes"; then
	AC_DEFINE(BUILD_GROWISOFS, 1, [define if you  want to build growisofs plugins])
else
	build_growisofs="no"
fi
AM_CONDITIONAL(BUILD_GROWISOFS, test x"$build_growisofs" = "xyes")

TRACKER_REQUIRED=0.10.0

AC_ARG_ENABLE(search,
			AS_HELP_STRING([--enable-search],[Build search pane (if a search backend is available) [[default=auto]]]),
			[case "$enableval" in
				"auto"|"yes"|"no") ;;
				*)         echo "Unknown option"; exit 2 ;;
			esac],
			[enable_search="auto"])

if test x"$enable_search" != "xno"; then
        # Try to figure out the tracker API version to use
        tracker_api="0.10"
        m4_foreach([VERSION], [[0.10], [0.12], [0.14], [0.16], [0.18], [1.0], [2.0]],
                              [PKG_CHECK_EXISTS([tracker-sparql-VERSION >= $TRACKER_REQUIRED],
                                                [tracker_api="VERSION"])
                              ])
fi

if test x"$enable_search" = "xauto"; then
        PKG_CHECK_EXISTS([tracker-sparql-$tracker_api >= $TRACKER_REQUIRED],
                         [enable_search=yes],
                         [enable_search=no])
fi

if test x"$enable_search" = "xyes"; then
	PKG_CHECK_MODULES(PARRILLADA_SEARCH, tracker-sparql-$tracker_api >= $TRACKER_REQUIRED)
	AC_DEFINE(BUILD_SEARCH, 1, [define if you  want to use search pane])
	AC_DEFINE(BUILD_TRACKER, 1, [define if you  want to use search pane])
	AC_SUBST(PARRILLADA_SEARCH_CFLAGS)
	AC_SUBST(PARRILLADA_SEARCH_LIBS)
	build_tracker="yes"
	build_search="yes"
else
	build_search="no"
fi

AM_CONDITIONAL(BUILD_SEARCH, test x"$build_search" = "xyes")
AM_CONDITIONAL(BUILD_TRACKER, test x"$build_tracker" = "xyes")

dnl ****************check for playlist (optional)**************
TOTEM_REQUIRED=2.29.1

AC_ARG_ENABLE(playlist,
			AS_HELP_STRING([--enable-playlist],[Build playlist pane (if totem-pl-parser is available)[[default=yes]]]),
			[enable_playlist=$enableval],
			[enable_playlist="yes"])

if test x"$enable_playlist" = "xyes"; then
	PKG_CHECK_MODULES(PARRILLADA_PL_PARSER, totem-plparser >= $TOTEM_REQUIRED, build_totem=yes, build_totem=no)
else
	build_totem="no"
fi

if test x"$build_totem" = "xyes"; then
	AC_DEFINE(BUILD_PLAYLIST, 1, [define if you  want to build playlist pane])
fi

AC_SUBST(PARRILLADA_PL_PARSER_CFLAGS)
AC_SUBST(PARRILLADA_PL_PARSER_LIBS)

AM_CONDITIONAL(BUILD_PLAYLIST, test x"$build_totem" = "xyes")

dnl ****************check for preview (optional)**************
AC_ARG_ENABLE(preview,
			AS_HELP_STRING([--enable-preview],[Build preview pane [[default=yes]]]),
			[enable_preview=$enableval],
			[enable_preview="yes"])

if test x"$enable_preview" = "xyes"; then
	PKG_CHECK_MODULES(PARRILLADA_GSTREAMER_BASE, gstreamer-plugins-base-1.0 >= $GSTREAMER_MODULE_REQUIRED, build_preview=yes, build_preview=no)
else
	build_preview="no"
fi

if test x"$build_preview" = "xyes"; then
	AC_DEFINE(BUILD_PREVIEW, 1, [define if you  want to build preview pane])
fi

AC_SUBST(PARRILLADA_GSTREAMER_BASE_CFLAGS)
AC_SUBST(PARRILLADA_GSTREAMER_BASE_LIBS)

AM_CONDITIONAL(BUILD_PREVIEW, test x"$build_preview" = "xyes")

dnl ****************check for inotify (optional)**************
AC_ARG_ENABLE(inotify,
			AS_HELP_STRING([--enable-inotify],[use inotify [[default=yes]]]),
			[enable_inotify=$enableval],
			[enable_inotify="yes"])

if test x"$enable_inotify" = "xyes"; then
	AC_DEFINE(BUILD_INOTIFY, 1, [define if you  want to build inotify])
fi
AM_CONDITIONAL(BUILD_INOTIFY, test x"$enable_inotify" = "xyes")

dnl ****** Check for introspection ***************************
GOBJECT_INTROSPECTION_CHECK([1.30.0])

dnl ****** Update mime, desktop and icon caches *******
AC_ARG_ENABLE(caches,
			AS_HELP_STRING([--enable-caches],[Run update-* to update mime, desktop and icon caches when installing [[default=yes]]]),
			[enable_caches=$enableval],
			[enable_caches="yes"])

AM_CONDITIONAL(UPDATE_CACHES, test x"$enable_caches" = "xyes")

dnl ****** INTERNATIONALIZATION **********************
GETTEXT_PACKAGE=parrillada
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

AM_GLIB_GNU_GETTEXT

IT_PROG_INTLTOOL([0.50])

if test "x${prefix}" = "xNONE"; then
  AC_DEFINE_UNQUOTED(PACKAGE_LOCALE_DIR, "${ac_default_prefix}/${DATADIRNAME}/locale", [Location of package locale files])
else
  AC_DEFINE_UNQUOTED(PACKAGE_LOCALE_DIR, "${prefix}/${DATADIRNAME}/locale", [Location of package locale files])
fi

dnl ****** DISABLE_DEPRECATED *************************
AC_ARG_ENABLE(deprecation_flags,
              [AC_HELP_STRING([--enable-deprecation-flags],
                              [use *_DISABLE_DEPRECATED flags @<:@default=no@:>@])],,
              [enable_deprecation_flags=no])

if test "x$enable_deprecation_flags" = "xyes"; then
   DISABLE_DEPRECATED_CFLAGS=$DISABLE_DEPRECATED
   AC_SUBST(DISABLE_DEPRECATED_CFLAGS)
fi

dnl ****** END ***********************

AC_CONFIG_FILES([
Makefile
docs/Makefile
docs/reference/Makefile
docs/reference/libparrillada-media/Makefile
docs/reference/libparrillada-burn/Makefile
docs/reference/libparrillada-media/version.xml
docs/reference/libparrillada-burn/version.xml
data/Makefile
data/parrillada.desktop.in
data/icons/Makefile
data/mime/Makefile
help/Makefile
caja/Makefile
caja/parrillada-caja.desktop.in
libparrillada-media/Makefile
libparrillada-media/parrillada-media.h
libparrillada-utils/Makefile
libparrillada-burn/Makefile
libparrillada-burn/parrillada-burn-lib.h
plugins/Makefile
plugins/audio2cue/Makefile
plugins/cdrdao/Makefile
plugins/cdrkit/Makefile
plugins/cdrtools/Makefile
plugins/growisofs/Makefile
plugins/libburnia/Makefile
plugins/transcode/Makefile
plugins/dvdcss/Makefile
plugins/dvdauthor/Makefile
plugins/checksum/Makefile
plugins/local-track/Makefile
plugins/vcdimager/Makefile
po/Makefile.in
src/Makefile
libparrillada-media3.pc
libparrillada-burn3.pc
])

AC_OUTPUT

echo
echo
echo "$PACKAGE configuration summary:"
echo "----------------------------------"

dnl 	Build libnotify support : ${build_libnotify}

echo "Version: $PARRILLADA_VERSION
	Using GTK+ 3
	Update caches: ${enable_caches}
	Build Caja extension : ${build_caja}
	Build inotify: ${enable_inotify}
	Build search pane : ${build_search}
	Build playlist pane : ${build_totem}
	Build Preview pane : ${build_preview}
	Plugins installed in : ${PARRILLADA_PLUGIN_DIRECTORY}
	Build cdrdao plugins : ${build_cdrdao}
	Build cdrtools plugins : ${build_cdrtools}
	Build cdrkit plugins : ${build_cdrkit}
	Build growisofs plugins : ${build_growisofs}
	Build libburnia plugins : ${build_libburnia}
	Build GObject-Introspection : ${found_introspection}
	Use *_DISABLE_DEPRECATED: ${enable_deprecation_flags}
"
echo
echo
