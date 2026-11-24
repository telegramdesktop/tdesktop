#include "law_fixture.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "platform/win/widget_sandbox_win.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#elif defined Q_OS_MAC
#include <QtGui/QImage>

#include <Cocoa/Cocoa.h>
#include <AVFoundation/AVFoundation.h>
#else
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#endif
