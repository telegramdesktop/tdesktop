TEMPLATE = subdirs

qtHaveModule(dbus) {
!mac:!win32:SUBDIRS += fcitx
!mac:!win32:SUBDIRS += ibus
}

contains(QT_CONFIG, xcb-plugin): SUBDIRS += compose


