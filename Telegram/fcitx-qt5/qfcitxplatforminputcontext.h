/***************************************************************************
 *   Copyright (C) 2012~2013 by CSSlayer                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#ifndef QFCITXPLATFORMINPUTCONTEXT_H
#define QFCITXPLATFORMINPUTCONTEXT_H

#include <qpa/qplatforminputcontext.h>
#include <QWindow>
#include <QKeyEvent>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QPointer>
#include <QFileSystemWatcher>
#include <QRect>
#include <xkbcommon/xkbcommon-compose.h>
#include "fcitxqtformattedpreedit.h"
#include "fcitxqtinputcontextproxy.h"

#define MAX_COMPOSE_LEN 7

class FcitxQtConnection;
class QFileSystemWatcher;
enum FcitxKeyEventType {
    FCITX_PRESS_KEY,
    FCITX_RELEASE_KEY
};

enum FcitxCapacityFlags {
    CAPACITY_NONE = 0,
    CAPACITY_CLIENT_SIDE_UI = (1 << 0),
    CAPACITY_PREEDIT = (1 << 1),
    CAPACITY_CLIENT_SIDE_CONTROL_STATE =  (1 << 2),
    CAPACITY_PASSWORD = (1 << 3),
    CAPACITY_FORMATTED_PREEDIT = (1 << 4),
    CAPACITY_CLIENT_UNFOCUS_COMMIT = (1 << 5),
    CAPACITY_SURROUNDING_TEXT = (1 << 6),
    CAPACITY_EMAIL = (1 << 7),
    CAPACITY_DIGIT = (1 << 8),
    CAPACITY_UPPERCASE = (1 << 9),
    CAPACITY_LOWERCASE = (1 << 10),
    CAPACITY_NOAUTOUPPERCASE = (1 << 11),
    CAPACITY_URL = (1 << 12),
    CAPACITY_DIALABLE = (1 << 13),
    CAPACITY_NUMBER = (1 << 14),
    CAPACITY_NO_ON_SCREEN_KEYBOARD = (1 << 15),
    CAPACITY_SPELLCHECK = (1 << 16),
    CAPACITY_NO_SPELLCHECK = (1 << 17),
    CAPACITY_WORD_COMPLETION = (1 << 18),
    CAPACITY_UPPERCASE_WORDS = (1 << 19),
    CAPACITY_UPPERCASE_SENTENCES = (1 << 20),
    CAPACITY_ALPHA = (1 << 21),
    CAPACITY_NAME = (1 << 22)
} ;

/** message type and flags */
enum FcitxMessageType {
    MSG_TYPE_FIRST = 0,
    MSG_TYPE_LAST = 6,
    MSG_TIPS = 0,           /**< Hint Text */
    MSG_INPUT = 1,          /**< User Input */
    MSG_INDEX = 2,          /**< Index Number */
    MSG_FIRSTCAND = 3,      /**< First candidate */
    MSG_USERPHR = 4,        /**< User Phrase */
    MSG_CODE = 5,           /**< Typed character */
    MSG_OTHER = 6,          /**< Other Text */
    MSG_NOUNDERLINE = (1 << 3), /**< backward compatible, no underline is a flag */
    MSG_HIGHLIGHT = (1 << 4), /**< highlight the preedit */
    MSG_DONOT_COMMIT_WHEN_UNFOCUS = (1 << 5), /**< backward compatible */
    MSG_REGULAR_MASK = 0x7 /**< regular color type mask */
};


enum FcitxKeyState {
    FcitxKeyState_None = 0,
    FcitxKeyState_Shift = 1 << 0,
    FcitxKeyState_CapsLock = 1 << 1,
    FcitxKeyState_Ctrl = 1 << 2,
    FcitxKeyState_Alt = 1 << 3,
    FcitxKeyState_Alt_Shift = FcitxKeyState_Alt | FcitxKeyState_Shift,
    FcitxKeyState_Ctrl_Shift = FcitxKeyState_Ctrl | FcitxKeyState_Shift,
    FcitxKeyState_Ctrl_Alt = FcitxKeyState_Ctrl | FcitxKeyState_Alt,
    FcitxKeyState_Ctrl_Alt_Shift = FcitxKeyState_Ctrl | FcitxKeyState_Alt | FcitxKeyState_Shift,
    FcitxKeyState_NumLock = 1 << 4,
    FcitxKeyState_Super = 1 << 6,
    FcitxKeyState_ScrollLock = 1 << 7,
    FcitxKeyState_MousePressed = 1 << 8,
    FcitxKeyState_HandledMask = 1 << 24,
    FcitxKeyState_IgnoredMask = 1 << 25,
    FcitxKeyState_Super2    = 1 << 26,
    FcitxKeyState_Hyper    = 1 << 27,
    FcitxKeyState_Meta     = 1 << 28,
    FcitxKeyState_UsedMask = 0x5c001fff
};

struct FcitxQtICData {
    FcitxQtICData() : capacity(0), proxy(0), surroundingAnchor(-1), surroundingCursor(-1) {}
    ~FcitxQtICData() {
        if (proxy && proxy->isValid()) {
            proxy->DestroyIC();
            delete proxy;
        }
    }
    QFlags<FcitxCapacityFlags> capacity;
    QPointer<FcitxQtInputContextProxy> proxy;
    QRect rect;
    QString surroundingText;
    int surroundingAnchor;
    int surroundingCursor;
};


class ProcessKeyWatcher : public QDBusPendingCallWatcher
{
    Q_OBJECT
public:
    ProcessKeyWatcher(const QKeyEvent& event, QWindow* window, const QDBusPendingCall &call, QObject *parent = 0) :
        QDBusPendingCallWatcher(call, parent)
       ,m_event(event.type(), event.key(), event.modifiers(),
                event.nativeScanCode(), event.nativeVirtualKey(), event.nativeModifiers(),
                event.text(), event.isAutoRepeat(), event.count())
       ,m_window(window)
    {
    }

    virtual ~ProcessKeyWatcher() {
    }

    const QKeyEvent& event() {
        return m_event;
    }

    QWindow* window() {
        return m_window.data();
    }

private:
    QKeyEvent m_event;
    QPointer<QWindow> m_window;
};

struct XkbContextDeleter
{
    static inline void cleanup(struct xkb_context* pointer)
    {
        if (pointer) xkb_context_unref(pointer);
    }
};

struct XkbComposeTableDeleter
{
    static inline void cleanup(struct xkb_compose_table* pointer)
    {
        if (pointer) xkb_compose_table_unref(pointer);
    }
};

struct XkbComposeStateDeleter
{
    static inline void cleanup(struct xkb_compose_state* pointer)
    {
        if (pointer) xkb_compose_state_unref(pointer);
    }
};

class FcitxQtInputMethodProxy;

class QFcitxPlatformInputContext : public QPlatformInputContext
{
    Q_OBJECT
public:
    QFcitxPlatformInputContext();
    virtual ~QFcitxPlatformInputContext();

    virtual bool filterEvent(const QEvent* event);
    virtual bool isValid() const;
    virtual void invokeAction(QInputMethod::Action , int cursorPosition);
    virtual void reset();
    virtual void commit();
    virtual void update(Qt::InputMethodQueries quries );
    virtual void setFocusObject(QObject* object);


public Q_SLOTS:
    void cursorRectChanged();
    void commitString(const QString& str);
    void updateFormattedPreedit(const FcitxQtFormattedPreeditList& preeditList, int cursorPos);
    void deleteSurroundingText(int offset, uint nchar);
    void forwardKey(uint keyval, uint state, int type);
    void createInputContextFinished(QDBusPendingCallWatcher* watcher);
    void connected();
    void cleanUp();
    void windowDestroyed(QObject* object);


private:
    void createInputContext(WId w);
    bool processCompose(uint keyval, uint state, FcitxKeyEventType event);
    bool checkAlgorithmically();
    bool checkCompactTable(const struct _FcitxComposeTableCompact *table);
    QKeyEvent* createKeyEvent(uint keyval, uint state, int type);


    void addCapacity(FcitxQtICData* data, QFlags<FcitxCapacityFlags> capacity, bool forceUpdate = false)
    {
        QFlags< FcitxCapacityFlags > newcaps = data->capacity | capacity;
        if (data->capacity != newcaps || forceUpdate) {
            data->capacity = newcaps;
            updateCapacity(data);
        }
    }

    void removeCapacity(FcitxQtICData* data, QFlags<FcitxCapacityFlags> capacity, bool forceUpdate = false)
    {
        QFlags< FcitxCapacityFlags > newcaps = data->capacity & (~capacity);
        if (data->capacity != newcaps || forceUpdate) {
            data->capacity = newcaps;
            updateCapacity(data);
        }
    }

    void updateCapacity(FcitxQtICData* data);
    void commitPreedit();
    void createICData(QWindow* w);
    FcitxQtInputContextProxy* validIC();
    FcitxQtInputContextProxy* validICByWindow(QWindow* window);
    FcitxQtInputContextProxy* validICByWId(WId wid);
    bool filterEventFallback(uint keyval, uint keycode, uint state, bool press);

    FcitxQtInputMethodProxy* m_improxy;
    uint m_compose_buffer[MAX_COMPOSE_LEN + 1];
    int m_n_compose;
    QString m_preedit;
    QString m_commitPreedit;
    FcitxQtFormattedPreeditList m_preeditList;
    int m_cursorPos;
    bool m_useSurroundingText;
    bool m_syncMode;
    FcitxQtConnection* m_connection;
    QString m_lastSurroundingText;
    int m_lastSurroundingAnchor;
    int m_lastSurroundingCursor;
    QHash<WId, FcitxQtICData*> m_icMap;
    QHash<QObject*, WId> m_windowToWidMap;
    WId m_lastWId;
    bool m_destroy;
    QScopedPointer<struct xkb_context, XkbContextDeleter> m_xkbContext;
    QScopedPointer<struct xkb_compose_table, XkbComposeTableDeleter>  m_xkbComposeTable;
    QScopedPointer<struct xkb_compose_state, XkbComposeStateDeleter> m_xkbComposeState;
private slots:
    void processKeyEventFinished(QDBusPendingCallWatcher*);
};

#endif // QFCITXPLATFORMINPUTCONTEXT_H
