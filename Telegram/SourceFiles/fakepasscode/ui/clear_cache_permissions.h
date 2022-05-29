#ifndef TELEGRAM_CLEAR_CACHE_PERMISSIONS_H
#define TELEGRAM_CLEAR_CACHE_PERMISSIONS_H


namespace FakePasscode {
#ifdef Q_OS_MAC
    void RequestCacheFolderMacosPermission();
#endif
}

#endif //TELEGRAM_CLEAR_CACHE_PERMISSIONS_H
