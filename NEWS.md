01/12/2017
Port to GTK3. (Used Brasero 3.12.1 as base.)
Requires Caja >= 1.17.0.
Install appdata in $(DATADIR)/metainfo.

20/12/2015

Fixed libdvdcss detection.
Migrate to yelp-tools.
Initial Support for caja 1.12
Remove beagle support.

03/01/2015

Fixed (again) gsettings schemas.

26/02/2014

Fixed gsettings schemas.

19/02/2014

Renamed project from brasero to parrillada.

15/11/2010

This is a bug fix release: 
Fix crash for Bug 632576 - Brasero segfaults just before it should start burning (Luis Medinas)
Fix for #630651 - Basero creating incomplete image (.ISO) files (Philippe Rouquier)

Translations:

Takayuki KUSANO <AE5T-KSN@asahi-net.or.jp>: Fix error in Japanese translation
Jorge González <jorgegonz@svn.gnome.org>: Updated Spanish translation
Carles Ferrando <carles.ferrando@gmail.com>: Updated Catalan (Valencian) translation
Kjartan Maraas <kmaraas@gnome.org>: Updated Norwegian bokmål translation from Torstein Adolf Winterseth
Žygimantas Beručka <zygis@gnome.org>: Updated Lithuanian translation
Wouter Bolsterlee <wbolster@gnome.org>:  Updated Dutch translation by Redmar
Joan Duran <jodufi@gmail.com>: Updated Catalan translation
Lucian Adrian Grijincu <lucian.grijincu@gmail.com>: Updated Romanian translation
Daniel Șerbănescu <cyber19rider@gmail.com> Lucian Adrian Grijincu <lucian.grijincu@gmail.com> : Updated Romanian translation
Christian Kirbach <Christian.Kirbach@googlemail.com>: [l10n] Updated German translation
YunQiang Su <wzssyqa@gmail.com>: Fix two problems in Simplified Chinese translation.
Changwoo Ryu <cwryu@debian.org>: Updated Korean translation
Mattias Põldaru <mahfiaz gmail com>: [l10n] Updated Estonian translation



27/09/2010: 2.32.0

Since 2.31.92:

Translations:
Takayuki KUSANO <AE5T-KSN@asahi-net.or.jp> Updated Japanese translation
Adrian Guniš <andygun696@gmail.com> Updated Czech translation
Inaki Larranaga Murgoitio <dooteo@zundan.com> Updated Basque language
Daniel S. Koda <danielskoda@gmail.com> Updated Brazilian Portuguese translation
Daniel Nylander <po@danielnylander.se> Updated Swedish translation
Mattias Põldaru <mahfiaz gmail com> [l10n] Updated Estonian translation

Changes:
Bump the gtk+ version requirements (Philippe Rouquier)
Fix #630178 - Deprecation of gtk_dialog_set_has_separator() in GTK+ 2.21.8 (Matthias Clasen)


Since 2.30.x:

Lots of new and updated translations.
Changes to meet the future GTK+3 requirements (brasero can now build with gtk+ 3) and make it GNOME3 ready.
Dropped glib-dbus dependency.
Move to GSettings.

+ lots of individual bug fixes

Homepage: http://www.gnome.org/projects/brasero

Please report bugs to: http://bugzilla.gnome.org/browse.cgi?product=brasero

Mailing List for User and Developer discussion: brasero-list@gnome.org

GIT Repository: http://git.gnome.org/cgit/brasero/

Thanks to all the people who contributed to this release through patches, translation, advices, artwork, bug reports.