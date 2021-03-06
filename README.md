parrillada
==========

A CD/DVD burning application for MATE Desktop forked from GNOME's Brasero by Philippe Rouquier.

Parrillada is a CD/DVD mastering tool for the MATE Desktop. It is designed to be simple and easy to use. 

Features:

Data CD/DVD:
- supports edition of discs contents (remove/move/rename files inside directories)
- can burn data CD/DVD on the fly
- automatic filtering for unwanted files (hidden files, broken/recursive symlinks, files not conforming to joliet standard, ...)
- supports multisession
- supports joliet extension
- can write the image to the hard drive

Audio CD:
- write CD-TEXT information (automatically found thanks to gstreamer)
- supports the edition of CD-TEXT information
- can burn audio CD on the fly
- can use all audio files handled by GStreamer local installation (ogg, flac, mp3, ...)
- can search for audio files inside dropped folders 
- can insert a pause
- can split a track

CD/DVD copy:
- can copy a CD/DVD to the hard drive
- can copy DVD and CD on the fly
- supports single-session data DVD
- supports any kind of CD
- can copy encrypted Video DVDs (needs libdvdcss)

Others:
- erase CD/DVD
- can save/load projects
- can burn CD/DVD images and cue files
- song, image and video previewer
- file change notification (requires kernel > 2.6.13)
- supports Drag and Drop / Cut'n'Paste from nautilus (and others apps)
- can use files on a network as long as the protocol is handled by gnome-vfs
- can search for files thanks to beagle (search is based on keywords or on file type)
- can display a playlist and its contents (note that playlists are automatically searched through beagle)
- all disc IO is done asynchronously to prevent the application from blocking
- Parrillada default backend is provided by cdrtools/cdrkit but libburn can be used as an alternative


Notes on plugins for advanced users

1. configuration

From the UI you can only configure (choose to use or not to use mostly) non essential plugins; that is all those that don't burn, blank, or image.
If you really want to choose which of the latters you want parrillada to use, one simple solution is to remove the offending plugin from parrillada plugin directory ("install_path"/lib/parrillada/plugins/) if you're sure that you won't want to use it.
You can also set priorities between plugins. They all have a hardcoded priority that can be overriden through Gconf. Each plugin has a key in "/org/mate/parrillada/config/priority".
If you set this key to -1 this turns off the plugin.
If you set this key to 0 this leaves the internal hardcoded priority - the default that basically lets parrillada decide what's best.
If you set this key to more than 0 then that priority will become the one of the plugin - the higher, the more it has chance to be picked up.

2. additional note

Some plugins have overlapping functionalities (i.e. libburn/wodim/cdrecord/growisofs, mkisofs/libisofs/genisoimage); but they don't always do the same things or sometimes they don't do it in the same way. Some plugins have a "speciality" where they are the best. That's why it's usually good to have them all around
As examples, from my experience:
- growisofs is good at handling DVD+RW and DVD-RW restricted overwrite
- cdrdao is best for on the fly CD copying
- libburn returns a progress when it blanks/formats

Build requirements:

- autoconf
- automake
- desktop-file-utils
- gettext
- gettext intltool
- intltool >= 0.50
- libappstream-glib
- libtool
- libxslt
- mate-common
- pkgconfig(dbus-glib-1) >= 0.7.2
- pkgconfig(glib-2.0) >= 2.29.14
- pkgconfig(gobject-introspection-1.0) >= 1.30.0
- pkgconfig(gstreamer-1.0) >= 0.11.92
- pkgconfig(gstreamer-base-1.0) >= 0.11.92
- pkgconfig(gstreamer-pbutils-1.0)
- pkgconfig(gstreamer-tag-1.0)
- pkgconfig(gstreamer-video-1.0)
- pkgconfig(gtk+-3.0) >= 2.99.0
- pkgconfig(gtk-doc)
- pkgconfig(ice)
- pkgconfig(libburn-1) = 0.4.0
- pkgconfig(libcanberra) >= 0.1
- pkgconfig(libcanberra-gtk3)
- pkgconfig(libisofs-1) = 0.6.4
- pkgconfig(libnotify) = 0.6.1
- pkgconfig(libxml-2.0) = 2.6.0
- pkgconfig(mate-desktop-2.0) >= 1.17.0
- pkgconfig(sm)
- pkgconfig(totem-plparser) >= 2.29.1
- pkgconfig(tracker-sparql-1.0) or pkgconfig(tracker-sparql-2.0)

Runtime requirements:
- gtk+ >= 3.0.0
- MATE Desktop >= 1.18.0.
  You may install version 1.6.2 if you use GTK2 MATE Desktop
- gstreamer >= 0.11.92
- DBus >= 1.x
- libxml2 >= 2.6.0
- cdrtools or cdrkit
- growisofs
- a fairly new kernel (>= 2.6.13 because of inotify) (optional)
- cairo
- libcanberra
- libburn (>=0.4.0) (optional)
- libisofs (>=0.6.2) (optional)

Installation:

   ./autogen.sh
   
   ./configure
   
   make
   
   sudo make install
