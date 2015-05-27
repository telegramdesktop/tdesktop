/***************************************************************************
 *   Copyright (C) 2011~2013 by CSSlayer                                   *
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

#include <QKeyEvent>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QInputMethod>
#include <QTextCharFormat>
#include <QPalette>
#include <QWindow>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformcursor.h>
#include <qpa/qwindowsysteminterface.h>

#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "keyserver_x11.h"

#include "qfcitxplatforminputcontext.h"
#include "fcitxqtinputcontextproxy.h"
#include "fcitxqtinputmethodproxy.h"
#include "fcitxqtconnection.h"
#include "keyuni.h"
#include "utils.h"

static bool key_filtered = false;

static bool
get_boolean_env(const char *name,
                 bool defval)
{
    const char *value = getenv(name);

    if (value == NULL)
        return defval;

    if (strcmp(value, "") == 0 ||
        strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0 ||
        strcmp(value, "False") == 0 ||
        strcmp(value, "FALSE") == 0)
        return false;

    return true;
}

static inline const char*
get_locale()
{
    const char* locale = getenv("LC_ALL");
    if (!locale)
        locale = getenv("LC_CTYPE");
    if (!locale)
        locale = getenv("LANG");
    if (!locale)
        locale = "C";

    return locale;
}

struct xkb_context* _xkb_context_new_helper()
{
    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context) {
        xkb_context_set_log_level(context, XKB_LOG_LEVEL_CRITICAL);
    }

    return context;
}

QFcitxPlatformInputContext::QFcitxPlatformInputContext() :
    m_connection(new FcitxQtConnection(this)),
    m_improxy(0),
    m_n_compose(0),
    m_cursorPos(0),
    m_useSurroundingText(false),
    m_syncMode(true),
    m_lastWId(0),
    m_destroy(false),
    m_xkbContext(_xkb_context_new_helper()),
    m_xkbComposeTable(m_xkbContext ? xkb_compose_table_new_from_locale(m_xkbContext.data(), get_locale(), XKB_COMPOSE_COMPILE_NO_FLAGS) : 0),
    m_xkbComposeState(m_xkbComposeTable ? xkb_compose_state_new(m_xkbComposeTable.data(), XKB_COMPOSE_STATE_NO_FLAGS) : 0)
{
    FcitxQtFormattedPreedit::registerMetaType();

    memset(m_compose_buffer, 0, sizeof(uint) * (MAX_COMPOSE_LEN + 1));
    connect(m_connection, SIGNAL(connected()), this, SLOT(connected()));
    connect(m_connection, SIGNAL(disconnected()), this, SLOT(cleanUp()));

    m_connection->startConnection();
}

QFcitxPlatformInputContext::~QFcitxPlatformInputContext()
{
    m_destroy = true;
    cleanUp();
}

void QFcitxPlatformInputContext::connected()
{
    if (!m_connection->isConnected())
        return;

    // qDebug() << "create Input Context" << m_connection->name();
    if (m_improxy) {
        delete m_improxy;
        m_improxy = 0;
    }
    m_improxy = new FcitxQtInputMethodProxy(m_connection->serviceName(), QLatin1String("/inputmethod"), *m_connection->connection(), this);

    QWindow* w = qApp->focusWindow();
    if (w)
        createICData(w);
}

void QFcitxPlatformInputContext::cleanUp()
{
    for(QHash<WId, FcitxQtICData *>::const_iterator i = m_icMap.constBegin(),
                                             e = m_icMap.constEnd(); i != e; ++i) {
        FcitxQtICData* data = i.value();

        if (data->proxy)
            delete data->proxy;
    }

    m_icMap.clear();

    if (m_improxy) {
        delete m_improxy;
        m_improxy = 0;
    }

    if (!m_destroy) {
        commitPreedit();
    }
}

bool QFcitxPlatformInputContext::isValid() const
{
    return true;
}

void QFcitxPlatformInputContext::invokeAction(QInputMethod::Action action, int cursorPosition)
{
    if (action == QInputMethod::Click
        && (cursorPosition <= 0 || cursorPosition >= m_preedit.length())
    )
    {
        // qDebug() << action << cursorPosition;
        commitPreedit();
    }
}

void QFcitxPlatformInputContext::commitPreedit()
{
    QObject *input = qApp->focusObject();
    if (!input)
        return;
    if (m_commitPreedit.length() <= 0)
        return;
    QInputMethodEvent e;
    e.setCommitString(m_commitPreedit);
    QCoreApplication::sendEvent(input, &e);
    m_commitPreedit.clear();
}


void QFcitxPlatformInputContext::reset()
{
    commitPreedit();
    FcitxQtInputContextProxy* proxy = validIC();
    if (proxy)
        proxy->Reset();
    if (m_xkbComposeState) {
        xkb_compose_state_reset(m_xkbComposeState.data());
    }
    QPlatformInputContext::reset();
}

void QFcitxPlatformInputContext::update(Qt::InputMethodQueries queries )
{
    QWindow* window = qApp->focusWindow();
    FcitxQtInputContextProxy* proxy = validICByWindow(window);
    if (!proxy)
        return;

    FcitxQtICData* data = m_icMap.value(window->winId());

    QInputMethod *method = qApp->inputMethod();
    QObject *input = qApp->focusObject();
    if (!input)
        return;

    QInputMethodQueryEvent query(queries);
    QGuiApplication::sendEvent(input, &query);

    if (queries & Qt::ImCursorRectangle) {
        cursorRectChanged();
    }

    if (queries & Qt::ImHints) {
        Qt::InputMethodHints hints = Qt::InputMethodHints(query.value(Qt::ImHints).toUInt());


#define CHECK_HINTS(_HINTS, _CAPACITY) \
    if (hints & _HINTS) \
        addCapacity(data, _CAPACITY); \
    else \
        removeCapacity(data, _CAPACITY);

        CHECK_HINTS(Qt::ImhNoAutoUppercase, CAPACITY_NOAUTOUPPERCASE)
        CHECK_HINTS(Qt::ImhPreferNumbers, CAPACITY_NUMBER)
        CHECK_HINTS(Qt::ImhPreferUppercase, CAPACITY_UPPERCASE)
        CHECK_HINTS(Qt::ImhPreferLowercase, CAPACITY_LOWERCASE)
        CHECK_HINTS(Qt::ImhNoPredictiveText, CAPACITY_NO_SPELLCHECK)
        CHECK_HINTS(Qt::ImhDigitsOnly, CAPACITY_DIGIT)
        CHECK_HINTS(Qt::ImhFormattedNumbersOnly, CAPACITY_NUMBER)
        CHECK_HINTS(Qt::ImhUppercaseOnly, CAPACITY_UPPERCASE)
        CHECK_HINTS(Qt::ImhLowercaseOnly, CAPACITY_LOWERCASE)
        CHECK_HINTS(Qt::ImhDialableCharactersOnly, CAPACITY_DIALABLE)
        CHECK_HINTS(Qt::ImhEmailCharactersOnly, CAPACITY_EMAIL)
    }

    bool setSurrounding = false;
    do {
        if (!m_useSurroundingText)
            break;
        if (!((queries & Qt::ImSurroundingText) && (queries & Qt::ImCursorPosition)))
            break;
        if (data->capacity.testFlag(CAPACITY_PASSWORD))
            break;
        QVariant var = query.value(Qt::ImSurroundingText);
        QVariant var1 = query.value(Qt::ImCursorPosition);
        QVariant var2 = query.value(Qt::ImAnchorPosition);
        if (!var.isValid() || !var1.isValid())
            break;
        QString text = var.toString();
        /* we don't want to waste too much memory here */
#define SURROUNDING_THRESHOLD 4096
        if (text.length() < SURROUNDING_THRESHOLD) {
            if (_utf8_check_string(text.toUtf8().data())) {
                addCapacity(data, CAPACITY_SURROUNDING_TEXT);

                int cursor = var1.toInt();
                int anchor;
                if (var2.isValid())
                    anchor = var2.toInt();
                else
                    anchor = cursor;
                if (data->surroundingText != text) {
                    data->surroundingText = text;
                    proxy->SetSurroundingText(text, cursor, anchor);
                }
                else {
                    if (data->surroundingAnchor != anchor ||
                        data->surroundingCursor != cursor)
                        proxy->SetSurroundingTextPosition(cursor, anchor);
                }
                data->surroundingCursor = cursor;
                data->surroundingAnchor = anchor;
                setSurrounding = true;
            }
        }
        if (!setSurrounding) {
            data->surroundingAnchor = -1;
            data->surroundingCursor = -1;
            data->surroundingText = QString::null;
            removeCapacity(data, CAPACITY_SURROUNDING_TEXT);
        }
    } while(0);
}

void QFcitxPlatformInputContext::commit()
{
    QPlatformInputContext::commit();
}

void QFcitxPlatformInputContext::setFocusObject(QObject* object)
{
    FcitxQtInputContextProxy* proxy = validICByWId(m_lastWId);
    if (proxy) {
        proxy->FocusOut();
    }

    QWindow *window = qApp->focusWindow();
    if (window) {
        m_lastWId = window->winId();
    } else {
        m_lastWId = 0;
        return;
    }
    proxy = validICByWindow(window);
    if (proxy)
        proxy->FocusIn();
    else {
        FcitxQtICData* data = m_icMap.value(window->winId());
        if (!data) {
            createICData(window);
            return;
        }
    }
}

void QFcitxPlatformInputContext::windowDestroyed(QObject* object)
{
    /* access QWindow is not possible here, so we use our own map to do so */
    WId wid = m_windowToWidMap.take(object);
    if (!wid)
        return;
    FcitxQtICData* data = m_icMap.take(wid);
    if (!data)
        return;

    delete data;
    // qDebug() << "Window Destroyed and we destroy IC correctly, horray!";
}

void QFcitxPlatformInputContext::cursorRectChanged()
{
    QWindow *inputWindow = qApp->focusWindow();
    if (!inputWindow)
        return;
    FcitxQtInputContextProxy* proxy = validICByWindow(inputWindow);
    if (!proxy)
        return;

    FcitxQtICData* data = m_icMap.value(inputWindow->winId());

    QRect r = qApp->inputMethod()->cursorRectangle().toRect();
    if(!r.isValid())
        return;

    r.moveTopLeft(inputWindow->mapToGlobal(r.topLeft()));

    if (data->rect != r) {
        data->rect = r;
        proxy->SetCursorRect(r.x(), r.y(), r.width(), r.height());
    }
}

void QFcitxPlatformInputContext::createInputContext(WId w)
{
    if (!m_connection->isConnected())
        return;

    // qDebug() << "create Input Context" << m_connection->connection()->name();

    if (m_improxy) {
        delete m_improxy;
        m_improxy = NULL;
    }
    m_improxy = new FcitxQtInputMethodProxy(m_connection->serviceName(), QLatin1String("/inputmethod"), *m_connection->connection(), this);

    if (!m_improxy->isValid())
        return;

    QFileInfo info(QCoreApplication::applicationFilePath());
    QDBusPendingReply< int, bool, uint, uint, uint, uint > result = m_improxy->CreateICv3(info.fileName(), QCoreApplication::applicationPid());
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(result);
    watcher->setProperty("wid", (qulonglong) w);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), this, SLOT(createInputContextFinished(QDBusPendingCallWatcher*)));
}

void QFcitxPlatformInputContext::createInputContextFinished(QDBusPendingCallWatcher* watcher)
{
    WId w = watcher->property("wid").toULongLong();
    FcitxQtICData* data = m_icMap.value(w);
    if (!data)
        return;

    QDBusPendingReply< int, bool, uint, uint, uint, uint > result = *watcher;

    do {
        if (result.isError()) {
            break;
        }

        if (!m_connection->isConnected())
            break;

        int id = qdbus_cast<int>(result.argumentAt(0));
        QString path = QString("/inputcontext_%1").arg(id);
        if (data->proxy) {
            delete data->proxy;
        }
        data->proxy = new FcitxQtInputContextProxy(m_connection->serviceName(), path, *m_connection->connection(), this);
        connect(data->proxy, SIGNAL(CommitString(QString)), this, SLOT(commitString(QString)));
        connect(data->proxy, SIGNAL(ForwardKey(uint, uint, int)), this, SLOT(forwardKey(uint, uint, int)));
        connect(data->proxy, SIGNAL(UpdateFormattedPreedit(FcitxQtFormattedPreeditList,int)), this, SLOT(updateFormattedPreedit(FcitxQtFormattedPreeditList,int)));
        connect(data->proxy, SIGNAL(DeleteSurroundingText(int,uint)), this, SLOT(deleteSurroundingText(int,uint)));

        if (data->proxy->isValid()) {
            QWindow* window = qApp->focusWindow();
            if (window && window->winId() == w)
                data->proxy->FocusIn();
        }

        QFlags<FcitxCapacityFlags> flag;
        flag |= CAPACITY_PREEDIT;
        flag |= CAPACITY_FORMATTED_PREEDIT;
        flag |= CAPACITY_CLIENT_UNFOCUS_COMMIT;
        m_useSurroundingText = get_boolean_env("FCITX_QT_ENABLE_SURROUNDING_TEXT", true);
        if (m_useSurroundingText)
            flag |= CAPACITY_SURROUNDING_TEXT;

        /*
         * event loop will cause some problem, so we tries to use async way.
         */
        m_syncMode = get_boolean_env("FCITX_QT_USE_SYNC", false);

        addCapacity(data, flag, true);
    } while(0);
    delete watcher;
}

void QFcitxPlatformInputContext::updateCapacity(FcitxQtICData* data)
{
    if (!data->proxy || !data->proxy->isValid())
        return;

    QDBusPendingReply< void > result = data->proxy->SetCapacity((uint) data->capacity);
}

void QFcitxPlatformInputContext::commitString(const QString& str)
{
    m_cursorPos = 0;
    m_preeditList.clear();
    m_commitPreedit.clear();
    QObject *input = qApp->focusObject();
    if (!input)
        return;

    QInputMethodEvent event;
    event.setCommitString(str);
    QCoreApplication::sendEvent(input, &event);
}

void QFcitxPlatformInputContext::updateFormattedPreedit(const FcitxQtFormattedPreeditList& preeditList, int cursorPos)
{
    QObject *input = qApp->focusObject();
    if (!input)
        return;
    if (cursorPos == m_cursorPos && preeditList == m_preeditList)
        return;
    m_preeditList = preeditList;
    m_cursorPos = cursorPos;
    QString str, commitStr;
    int pos = 0;
    QList<QInputMethodEvent::Attribute> attrList;
    Q_FOREACH(const FcitxQtFormattedPreedit& preedit, preeditList)
    {
        str += preedit.string();
        if (!(preedit.format() & MSG_DONOT_COMMIT_WHEN_UNFOCUS))
            commitStr += preedit.string();
        QTextCharFormat format;
        if ((preedit.format() & MSG_NOUNDERLINE) == 0) {
            format.setUnderlineStyle(QTextCharFormat::DashUnderline);
        }
        if (preedit.format() & MSG_HIGHLIGHT) {
            QBrush brush;
            QPalette palette;
            palette = QGuiApplication::palette();
            format.setBackground(QBrush(QColor(palette.color(QPalette::Active, QPalette::Highlight))));
            format.setForeground(QBrush(QColor(palette.color(QPalette::Active, QPalette::HighlightedText))));
        }
        attrList.append(QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, pos, preedit.string().length(), format));
        pos += preedit.string().length();
    }

    QByteArray array = str.toUtf8();
    array.truncate(cursorPos);
    cursorPos = QString::fromUtf8(array).length();

    attrList.append(QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, cursorPos, 1, 0));
    m_preedit = str;
    m_commitPreedit = commitStr;
    QInputMethodEvent event(str, attrList);
    QCoreApplication::sendEvent(input, &event);
    update(Qt::ImCursorRectangle);
}

void QFcitxPlatformInputContext::deleteSurroundingText(int offset, uint nchar)
{
    QObject *input = qApp->focusObject();
    if (!input)
        return;

    QInputMethodEvent event;
    event.setCommitString("", offset, nchar);
    QCoreApplication::sendEvent(input, &event);
}

void QFcitxPlatformInputContext::forwardKey(uint keyval, uint state, int type)
{
    QObject *input = qApp->focusObject();
    if (input != NULL) {
        key_filtered = true;
        QKeyEvent *keyevent = createKeyEvent(keyval, state, type);
        QCoreApplication::sendEvent(input, keyevent);
        delete keyevent;
        key_filtered = false;
    }
}

void QFcitxPlatformInputContext::createICData(QWindow* w)
{
    FcitxQtICData* data = m_icMap.value(w->winId());
    if (!data) {
        data = new FcitxQtICData;
        m_icMap[w->winId()] = data;
        m_windowToWidMap[w] = w->winId();
        connect(w, SIGNAL(destroyed(QObject*)), this, SLOT(windowDestroyed(QObject*)));
    }
    createInputContext(w->winId());
}

QKeyEvent* QFcitxPlatformInputContext::createKeyEvent(uint keyval, uint state, int type)
{
    Qt::KeyboardModifiers qstate = Qt::NoModifier;

    int count = 1;
    if (state & FcitxKeyState_Alt) {
        qstate |= Qt::AltModifier;
        count ++;
    }

    if (state & FcitxKeyState_Shift) {
        qstate |= Qt::ShiftModifier;
        count ++;
    }

    if (state & FcitxKeyState_Ctrl) {
        qstate |= Qt::ControlModifier;
        count ++;
    }

    int key;
    symToKeyQt(keyval, key);

    QKeyEvent* keyevent = new QKeyEvent(
        (type == FCITX_PRESS_KEY) ? (QEvent::KeyPress) : (QEvent::KeyRelease),
        key,
        qstate,
        QString(),
        false,
        count
    );

    return keyevent;
}

bool QFcitxPlatformInputContext::filterEvent(const QEvent* event)
{
    do {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
            break;
        }

        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        quint32 keyval = keyEvent->nativeVirtualKey();
        quint32 keycode = keyEvent->nativeScanCode();
        quint32 state = keyEvent->nativeModifiers();
        bool press = keyEvent->type() == QEvent::KeyPress;

        if (key_filtered) {
            break;
        }

        if (!inputMethodAccepted())
            break;

        QObject *input = qApp->focusObject();

        if (!input) {
            break;
        }

        FcitxQtInputContextProxy* proxy = validICByWindow(qApp->focusWindow());

        if (!proxy) {
            if (filterEventFallback(keyval, keycode, state, press)) {
                return true;
            } else {
                break;
            }
        }

        proxy->FocusIn();

        QDBusPendingReply< int > reply = proxy->ProcessKeyEvent(keyval,
                                                                keycode,
                                                                state,
                                                                (press) ? FCITX_PRESS_KEY : FCITX_RELEASE_KEY,
                                                                QDateTime::currentDateTime().toTime_t());


        if (Q_UNLIKELY(m_syncMode)) {
            reply.waitForFinished();

            if (!m_connection->isConnected() || !reply.isFinished() || reply.isError() || reply.value() <= 0) {
                if (filterEventFallback(keyval, keycode, state, press)) {
                    return true;
                } else {
                    break;
                }
            } else {
                update(Qt::ImCursorRectangle);
                return true;
            }
        }
        else {
            ProcessKeyWatcher* watcher = new ProcessKeyWatcher(*keyEvent, qApp->focusWindow(), reply, this);
            connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
                    this, SLOT(processKeyEventFinished(QDBusPendingCallWatcher*)));
            return true;
        }
    } while(0);
    return QPlatformInputContext::filterEvent(event);
}

void QFcitxPlatformInputContext::processKeyEventFinished(QDBusPendingCallWatcher* w)
{
    ProcessKeyWatcher* watcher = static_cast<ProcessKeyWatcher*>(w);
    QDBusPendingReply< int > result(*watcher);
    bool filtered = false;

    QWindow* window = watcher->window();
    // if window is already destroyed, we can only throw this event away.
    if (!window) {
        return;
    }

    const QKeyEvent& keyEvent = watcher->event();

    // use same variable name as in QXcbKeyboard::handleKeyEvent
    QEvent::Type type = keyEvent.type();
    int qtcode = keyEvent.key();
    Qt::KeyboardModifiers modifiers = keyEvent.modifiers();
    quint32 code = keyEvent.nativeScanCode();
    quint32 sym = keyEvent.nativeVirtualKey();
    quint32 state = keyEvent.nativeModifiers();
    QString string = keyEvent.text();
    bool isAutoRepeat = keyEvent.isAutoRepeat();
    ulong time = keyEvent.timestamp();

    if (result.isError() || result.value() <= 0) {
        filtered = filterEventFallback(sym, code, state, type == QEvent::KeyPress);
    } else {
        filtered = true;
    }

    if (!result.isError()) {
        update(Qt::ImCursorRectangle);
    }

    if (!filtered) {
        // copied from QXcbKeyboard::handleKeyEvent()
        if (type == QEvent::KeyPress && qtcode == Qt::Key_Menu) {
            const QPoint globalPos = window->screen()->handle()->cursor()->pos();
            const QPoint pos = window->mapFromGlobal(globalPos);            QWindowSystemInterface::handleContextMenuEvent(window, false, pos, globalPos, modifiers);
        }
        QWindowSystemInterface::handleExtendedKeyEvent(window, time, type, qtcode, modifiers,
                                                       code, sym, state, string, isAutoRepeat);
    }

    delete watcher;
}


bool QFcitxPlatformInputContext::filterEventFallback(uint keyval, uint keycode, uint state, bool press)
{
    Q_UNUSED(keycode);
    if (processCompose(keyval, state, (press) ? FCITX_PRESS_KEY : FCITX_RELEASE_KEY)) {
        return true;
    }
    return false;
}

FcitxQtInputContextProxy* QFcitxPlatformInputContext::validIC()
{
    if (m_icMap.isEmpty()) {
        return 0;
    }
    QWindow* window = qApp->focusWindow();
    return validICByWindow(window);
}

FcitxQtInputContextProxy* QFcitxPlatformInputContext::validICByWId(WId wid)
{
    if (m_icMap.isEmpty()) {
        return 0;
    }
    FcitxQtICData* icData = m_icMap.value(wid);
    if (!icData)
        return 0;
    if (icData->proxy.isNull()) {
        return 0;
    } else if (icData->proxy->isValid()) {
        return icData->proxy.data();
    }
    return 0;
}

FcitxQtInputContextProxy* QFcitxPlatformInputContext::validICByWindow(QWindow* w)
{
    if (!w) {
        return 0;
    }

    if (m_icMap.isEmpty()) {
        return 0;
    }
    return validICByWId(w->winId());
}


bool QFcitxPlatformInputContext::processCompose(uint keyval, uint state, FcitxKeyEventType event)
{
    Q_UNUSED(state);

    if (!m_xkbComposeTable || event == FCITX_RELEASE_KEY)
        return false;

    struct xkb_compose_state* xkbComposeState = m_xkbComposeState.data();

    enum xkb_compose_feed_result result = xkb_compose_state_feed(xkbComposeState, keyval);
    if (result == XKB_COMPOSE_FEED_IGNORED) {
        return false;
    }

    enum xkb_compose_status status = xkb_compose_state_get_status(xkbComposeState);
    if (status == XKB_COMPOSE_NOTHING) {
        return 0;
    } else if (status == XKB_COMPOSE_COMPOSED) {
        char buffer[] = {'\0', '\0', '\0', '\0', '\0', '\0', '\0'};
        int length = xkb_compose_state_get_utf8(xkbComposeState, buffer, sizeof(buffer));
        xkb_compose_state_reset(xkbComposeState);
        if (length != 0) {
            commitString(QString::fromUtf8(buffer));
        }

    } else if (status == XKB_COMPOSE_CANCELLED) {
        xkb_compose_state_reset(xkbComposeState);
    }

    return true;
}


// kate: indent-mode cstyle; space-indent on; indent-width 0;
