/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwidgetlinecontrol_p.h"

#ifndef QT_NO_LINEEDIT

#include "qabstractitemview.h"
#include "qclipboard.h"
#include <private/qguiapplication_p.h>
#include <qpa/qplatformtheme.h>
#include <qstylehints.h>
#include <QtCore/QLocale>
#ifndef QT_NO_ACCESSIBILITY
#include "qaccessible.h"
#endif

#include "qapplication.h"
#ifndef QT_NO_GRAPHICSVIEW
#include "qgraphicssceneevent.h"
#endif

QT_BEGIN_NAMESPACE


/*!
   \internal

   Updates the internal text layout. Returns the ascent of the
   created QTextLine.
*/
int QWidgetLineControl::redoTextLayout() const
{
    m_textLayout.clearLayout();

    m_textLayout.beginLayout();
    QTextLine l = m_textLayout.createLine();
    m_textLayout.endLayout();

#if defined(Q_DEAD_CODE_FROM_QT4_MAC)
    if (m_threadChecks)
        m_textLayoutThread = QThread::currentThread();
#endif

    return qRound(l.ascent());
}

/*!
    \internal

    Updates the display text based of the current edit text
    If the text has changed will emit displayTextChanged()
*/
void QWidgetLineControl::updateDisplayText(bool forceUpdate)
{
    QString orig = m_textLayout.text();
    QString str;
    if (m_echoMode == QLineEdit::NoEcho)
        str = QString::fromLatin1("");
    else
        str = m_text;

    if (m_echoMode == QLineEdit::Password) {
        str.fill(m_passwordCharacter);
        if (m_passwordEchoTimer != 0 && m_cursor > 0 && m_cursor <= m_text.length()) {
            int cursor = m_cursor - 1;
            QChar uc = m_text.at(cursor);
            str[cursor] = uc;
            if (cursor > 0 && uc.isLowSurrogate()) {
                // second half of a surrogate, check if we have the first half as well,
                // if yes restore both at once
                uc = m_text.at(cursor - 1);
                if (uc.isHighSurrogate())
                    str[cursor - 1] = uc;
            }
        }
    } else if (m_echoMode == QLineEdit::PasswordEchoOnEdit && !m_passwordEchoEditing) {
        str.fill(m_passwordCharacter);
    }

    // replace certain non-printable characters with spaces (to avoid
    // drawing boxes when using fonts that don't have glyphs for such
    // characters)
    QChar* uc = str.data();
    for (int i = 0; i < (int)str.length(); ++i) {
        if ((uc[i] < 0x20 && uc[i] != 0x09)
            || uc[i] == QChar::LineSeparator
            || uc[i] == QChar::ParagraphSeparator
            || uc[i] == QChar::ObjectReplacementCharacter)
            uc[i] = QChar(0x0020);
    }

    m_textLayout.setText(str);

    QTextOption option = m_textLayout.textOption();
    option.setTextDirection(m_layoutDirection);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    m_textLayout.setTextOption(option);

    m_ascent = redoTextLayout();

    if (str != orig || forceUpdate)
        emit displayTextChanged(str);
}

#ifndef QT_NO_CLIPBOARD
/*!
    \internal

    Copies the currently selected text into the clipboard using the given
    \a mode.

    \note If the echo mode is set to a mode other than Normal then copy
    will not work.  This is to prevent using copy as a method of bypassing
    password features of the line control.
*/
void QWidgetLineControl::copy(QClipboard::Mode mode) const
{
    QString t = selectedText();
    if (!t.isEmpty() && m_echoMode == QLineEdit::Normal) {
        disconnect(QApplication::clipboard(), SIGNAL(selectionChanged()), this, 0);
        QApplication::clipboard()->setText(t, mode);
        connect(QApplication::clipboard(), SIGNAL(selectionChanged()),
                   this, SLOT(_q_clipboardChanged()));
    }
}

/*!
    \internal

    Inserts the text stored in the application clipboard into the line
    control.

    \sa insert()
*/
void QWidgetLineControl::paste(QClipboard::Mode clipboardMode)
{
    QString clip = QApplication::clipboard()->text(clipboardMode);
    if (!clip.isEmpty() || hasSelectedText()) {
        separate(); //make it a separate undo/redo command
        insert(clip);
        separate();
    }
}

#endif // !QT_NO_CLIPBOARD

/*!
    \internal
*/
void QWidgetLineControl::commitPreedit()
{
#ifndef QT_NO_IM
    if (!composeMode())
        return;

    QGuiApplication::inputMethod()->commit();
    if (!composeMode())
        return;

    m_preeditCursor = 0;
    setPreeditArea(-1, QString());
    m_textLayout.clearAdditionalFormats();
    updateDisplayText(/*force*/ true);
#endif
}


/*!
    \internal

    Handles the behavior for the backspace key or function.
    Removes the current selection if there is a selection, otherwise
    removes the character prior to the cursor position.

    \sa del()
*/
void QWidgetLineControl::backspace()
{
    int priorState = m_undoState;
    if (hasSelectedText()) {
        removeSelectedText();
    } else if (m_cursor) {
            --m_cursor;
            if (m_maskData)
                m_cursor = prevMaskBlank(m_cursor);
            QChar uc = m_text.at(m_cursor);
            if (m_cursor > 0 && uc.isLowSurrogate()) {
                // second half of a surrogate, check if we have the first half as well,
                // if yes delete both at once
                uc = m_text.at(m_cursor - 1);
                if (uc.isHighSurrogate()) {
                    internalDelete(true);
                    --m_cursor;
                }
            }
            internalDelete(true);
    }
    finishChange(priorState);
}

/*!
    \internal

    Handles the behavior for the delete key or function.
    Removes the current selection if there is a selection, otherwise
    removes the character after the cursor position.

    \sa del()
*/
void QWidgetLineControl::del()
{
    int priorState = m_undoState;
    if (hasSelectedText()) {
        removeSelectedText();
    } else {
        int n = textLayout()->nextCursorPosition(m_cursor) - m_cursor;
        while (n--)
            internalDelete();
    }
    finishChange(priorState);
}

/*!
    \internal

    Inserts the given \a newText at the current cursor position.
    If there is any selected text it is removed prior to insertion of
    the new text.
*/
void QWidgetLineControl::insert(const QString &newText)
{
    int priorState = m_undoState;
    removeSelectedText();
    internalInsert(newText);
    finishChange(priorState);
}

/*!
    \internal

    Clears the line control text.
*/
void QWidgetLineControl::clear()
{
    int priorState = m_undoState;
    m_selstart = 0;
    m_selend = m_text.length();
    removeSelectedText();
    separate();
    finishChange(priorState, /*update*/false, /*edited*/false);
}
/*!
    \internal

    Undoes the previous operation.
*/

void QWidgetLineControl::undo()
{
    // Undo works only for clearing the line when in any of password the modes
    if (m_echoMode == QLineEdit::Normal) {
        internalUndo();
        finishChange(-1, true);
    } else {
        cancelPasswordEchoTimer();
        clear();
    }
}

/*!
    \internal

    Sets \a length characters from the given \a start position as selected.
    The given \a start position must be within the current text for
    the line control.  If \a length characters cannot be selected, then
    the selection will extend to the end of the current text.
*/
void QWidgetLineControl::setSelection(int start, int length)
{
    commitPreedit();

    if(start < 0 || start > (int)m_text.length()){
        qWarning("QWidgetLineControl::setSelection: Invalid start position");
        return;
    }

    if (length > 0) {
        if (start == m_selstart && start + length == m_selend && m_cursor == m_selend)
            return;
        m_selstart = start;
        m_selend = qMin(start + length, (int)m_text.length());
        m_cursor = m_selend;
    } else if (length < 0){
        if (start == m_selend && start + length == m_selstart && m_cursor == m_selstart)
            return;
        m_selstart = qMax(start + length, 0);
        m_selend = start;
        m_cursor = m_selstart;
    } else if (m_selstart != m_selend) {
        m_selstart = 0;
        m_selend = 0;
        m_cursor = start;
    } else {
        m_cursor = start;
        emitCursorPositionChanged();
        return;
    }
    emit selectionChanged();
    emitCursorPositionChanged();
}

void QWidgetLineControl::_q_clipboardChanged()
{
}

void QWidgetLineControl::_q_deleteSelected()
{
    if (!hasSelectedText())
        return;

    int priorState = m_undoState;
    emit resetInputContext();
    removeSelectedText();
    separate();
    finishChange(priorState);
}

/*!
    \internal

    Initializes the line control with a starting text value of \a txt.
*/
void QWidgetLineControl::init(const QString &txt)
{
    m_textLayout.setCacheEnabled(true);
    m_text = txt;
    updateDisplayText();
    m_cursor = m_text.length();
    if (const QPlatformTheme *theme = QGuiApplicationPrivate::platformTheme()) {
        m_keyboardScheme = theme->themeHint(QPlatformTheme::KeyboardScheme).toInt();
        m_passwordMaskDelay = theme->themeHint(QPlatformTheme::PasswordMaskDelay).toInt();
    }
    // Generalize for X11
    if (m_keyboardScheme == QPlatformTheme::KdeKeyboardScheme
        || m_keyboardScheme == QPlatformTheme::GnomeKeyboardScheme
        || m_keyboardScheme == QPlatformTheme::CdeKeyboardScheme) {
        m_keyboardScheme = QPlatformTheme::X11KeyboardScheme;
    }
}

/*!
    \internal

    Sets the password echo editing to \a editing.  If password echo editing
    is true, then the text of the password is displayed even if the echo
    mode is set to QLineEdit::PasswordEchoOnEdit.  Password echoing editing
    does not affect other echo modes.
*/
void QWidgetLineControl::updatePasswordEchoEditing(bool editing)
{
    cancelPasswordEchoTimer();
    m_passwordEchoEditing = editing;
    updateDisplayText();
}

/*!
    \internal

    Returns the cursor position of the given \a x pixel value in relation
    to the displayed text.  The given \a betweenOrOn specified what kind
    of cursor position is requested.
*/
int QWidgetLineControl::xToPos(int x, QTextLine::CursorPosition betweenOrOn) const
{
    return textLayout()->lineAt(0).xToCursor(x, betweenOrOn);
}

/*!
    \internal

    Returns the bounds of the current cursor, as defined as a
    between characters cursor.
*/
QRect QWidgetLineControl::cursorRect() const
{
    QTextLine l = textLayout()->lineAt(0);
    int c = m_cursor;
    if (m_preeditCursor != -1)
        c += m_preeditCursor;
    int cix = qRound(l.cursorToX(c));
    int w = m_cursorWidth;
    int ch = l.height() + 1;

    return QRect(cix-5, 0, w+9, ch);
}

/*!
    \internal

    Fixes the current text so that it is valid given any set validators.

    Returns \c true if the text was changed.  Otherwise returns \c false.
*/
bool QWidgetLineControl::fixup() // this function assumes that validate currently returns != Acceptable
{
#ifndef QT_NO_VALIDATOR
    if (m_validator) {
        QString textCopy = m_text;
        int cursorCopy = m_cursor;
        m_validator->fixup(textCopy);
        if (m_validator->validate(textCopy, cursorCopy) == QValidator::Acceptable) {
            if (textCopy != m_text || cursorCopy != m_cursor)
                internalSetText(textCopy, cursorCopy, false);
            return true;
        }
    }
#endif
    return false;
}

/*!
    \internal

    Moves the cursor to the given position \a pos.   If \a mark is true will
    adjust the currently selected text.
*/
void QWidgetLineControl::moveCursor(int pos, bool mark)
{
    commitPreedit();

    if (pos != m_cursor) {
        separate();
        if (m_maskData)
            pos = pos > m_cursor ? nextMaskBlank(pos) : prevMaskBlank(pos);
    }
    if (mark) {
        int anchor;
        if (m_selend > m_selstart && m_cursor == m_selstart)
            anchor = m_selend;
        else if (m_selend > m_selstart && m_cursor == m_selend)
            anchor = m_selstart;
        else
            anchor = m_cursor;
        m_selstart = qMin(anchor, pos);
        m_selend = qMax(anchor, pos);
        updateDisplayText();
    } else {
        internalDeselect();
    }
    m_cursor = pos;
    if (mark || m_selDirty) {
        m_selDirty = false;
        emit selectionChanged();
    }
    emitCursorPositionChanged();
}

/*!
    \internal

    Applies the given input method event \a event to the text of the line
    control
*/
void QWidgetLineControl::processInputMethodEvent(QInputMethodEvent *event)
{
    int priorState = -1;
    bool isGettingInput = !event->commitString().isEmpty()
            || event->preeditString() != preeditAreaText()
            || event->replacementLength() > 0;
    bool cursorPositionChanged = false;
    bool selectionChange = false;

    if (isGettingInput) {
        // If any text is being input, remove selected text.
        priorState = m_undoState;
        if (echoMode() == QLineEdit::PasswordEchoOnEdit && !passwordEchoEditing()) {
            updatePasswordEchoEditing(true);
            m_selstart = 0;
            m_selend = m_text.length();
        }
        removeSelectedText();
    }

    int c = m_cursor; // cursor position after insertion of commit string
    if (event->replacementStart() <= 0)
        c += event->commitString().length() - qMin(-event->replacementStart(), event->replacementLength());

    m_cursor += event->replacementStart();
    if (m_cursor < 0)
        m_cursor = 0;

    // insert commit string
    if (event->replacementLength()) {
        m_selstart = m_cursor;
        m_selend = m_selstart + event->replacementLength();
        removeSelectedText();
    }
    if (!event->commitString().isEmpty()) {
        internalInsert(event->commitString());
        cursorPositionChanged = true;
    }

    m_cursor = qBound(0, c, m_text.length());

    for (int i = 0; i < event->attributes().size(); ++i) {
        const QInputMethodEvent::Attribute &a = event->attributes().at(i);
        if (a.type == QInputMethodEvent::Selection) {
            m_cursor = qBound(0, a.start + a.length, m_text.length());
            if (a.length) {
                m_selstart = qMax(0, qMin(a.start, m_text.length()));
                m_selend = m_cursor;
                if (m_selend < m_selstart) {
                    qSwap(m_selstart, m_selend);
                }
                selectionChange = true;
            } else {
                if (m_selstart != m_selend)
                    selectionChange = true;
                m_selstart = m_selend = 0;
            }
            cursorPositionChanged = true;
        }
    }
#ifndef QT_NO_IM
    setPreeditArea(m_cursor, event->preeditString());
#endif //QT_NO_IM
    const int oldPreeditCursor = m_preeditCursor;
    m_preeditCursor = event->preeditString().length();
    m_hideCursor = false;
    QList<QTextLayout::FormatRange> formats;
    for (int i = 0; i < event->attributes().size(); ++i) {
        const QInputMethodEvent::Attribute &a = event->attributes().at(i);
        if (a.type == QInputMethodEvent::Cursor) {
            m_preeditCursor = a.start;
            m_hideCursor = !a.length;
        } else if (a.type == QInputMethodEvent::TextFormat) {
            QTextCharFormat f = qvariant_cast<QTextFormat>(a.value).toCharFormat();
            if (f.isValid()) {
                QTextLayout::FormatRange o;
                o.start = a.start + m_cursor;
                o.length = a.length;
                o.format = f;
                formats.append(o);
            }
        }
    }
    m_textLayout.setAdditionalFormats(formats);
    updateDisplayText(/*force*/ true);
    if (cursorPositionChanged)
        emitCursorPositionChanged();
    else if (m_preeditCursor != oldPreeditCursor)
        emit updateMicroFocus();

    if (isGettingInput)
        finishChange(priorState);

    if (selectionChange)
        emit selectionChanged();
}

/*!
    \internal

    Draws the display text for the line control using the given
    \a painter, \a clip, and \a offset.  Which aspects of the display text
    are drawn is specified by the given \a flags.

    If the flags contain DrawSelections, then the selection or input mask
    backgrounds and foregrounds will be applied before drawing the text.

    If the flags contain DrawCursor a cursor of the current cursorWidth()
    will be drawn after drawing the text.

    The display text will only be drawn if the flags contain DrawText
*/
void QWidgetLineControl::draw(QPainter *painter, const QPoint &offset, const QRect &clip, int flags)
{
    QVector<QTextLayout::FormatRange> selections;
    if (flags & DrawSelections) {
        QTextLayout::FormatRange o;
        if (m_selstart < m_selend) {
            o.start = m_selstart;
            o.length = m_selend - m_selstart;
            o.format.setBackground(m_palette.brush(QPalette::Highlight));
            o.format.setForeground(m_palette.brush(QPalette::HighlightedText));
        } else {
            // mask selection
            if(!m_blinkPeriod || m_blinkStatus){
                o.start = m_cursor;
                o.length = 1;
                o.format.setBackground(m_palette.brush(QPalette::Text));
                o.format.setForeground(m_palette.brush(QPalette::Window));
            }
        }
        selections.append(o);
    }

    if (flags & DrawText)
        textLayout()->draw(painter, offset, selections, clip);

    if (flags & DrawCursor){
        int cursor = m_cursor;
        if (m_preeditCursor != -1)
            cursor += m_preeditCursor;
        if (!m_hideCursor && (!m_blinkPeriod || m_blinkStatus))
            textLayout()->drawCursor(painter, offset, cursor, m_cursorWidth);
    }
}

/*!
    \internal

    Sets the selection to cover the word at the given cursor position.
    The word boundaries are defined by the behavior of QTextLayout::SkipWords
    cursor mode.
*/
void QWidgetLineControl::selectWordAtPos(int cursor)
{
    int next = cursor + 1;
    if(next > end())
        --next;
    int c = textLayout()->previousCursorPosition(next, QTextLayout::SkipWords);
    moveCursor(c, false);
    // ## text layout should support end of words.
    int end = textLayout()->nextCursorPosition(c, QTextLayout::SkipWords);
    while (end > cursor && m_text[end-1].isSpace())
        --end;
    moveCursor(end, true);
}

/*!
    \internal

    Completes a change to the line control text.  If the change is not valid
    will undo the line control state back to the given \a validateFromState.

    If \a edited is true and the change is valid, will emit textEdited() in
    addition to textChanged().  Otherwise only emits textChanged() on a valid
    change.

    The \a update value is currently unused.
*/
bool QWidgetLineControl::finishChange(int validateFromState, bool update, bool edited)
{
    Q_UNUSED(update)

    if (m_textDirty) {
        // do validation
        bool wasValidInput = m_validInput;
        m_validInput = true;
#ifndef QT_NO_VALIDATOR
        if (m_validator) {
            QString textCopy = m_text;
            int cursorCopy = m_cursor;
            m_validInput = (m_validator->validate(textCopy, cursorCopy) != QValidator::Invalid);
            if (m_validInput) {
                if (m_text != textCopy) {
                    internalSetText(textCopy, cursorCopy, false);
                    return true;
                }
                m_cursor = cursorCopy;
            }
        }
#endif
        if (validateFromState >= 0 && wasValidInput && !m_validInput) {
            if (m_transactions.count())
                return false;
            internalUndo(validateFromState);
            m_history.resize(m_undoState);
            if (m_modifiedState > m_undoState)
                m_modifiedState = -1;
            m_validInput = true;
            m_textDirty = false;
        }
        updateDisplayText();

        if (m_textDirty) {
            m_textDirty = false;
            QString actualText = text();
            if (edited)
                emit textEdited(actualText);
            emit textChanged(actualText);
        }
    }
    if (m_selDirty) {
        m_selDirty = false;
        emit selectionChanged();
    }
    if (m_cursor == m_lastCursorPos)
        updateMicroFocus();
    emitCursorPositionChanged();
    return true;
}

/*!
    \internal

    An internal function for setting the text of the line control.
*/
void QWidgetLineControl::internalSetText(const QString &txt, int pos, bool edited)
{
    cancelPasswordEchoTimer();
    internalDeselect();
    emit resetInputContext();
    QString oldText = m_text;
    if (m_maskData) {
        m_text = maskString(0, txt, true);
        m_text += clearString(m_text.length(), m_maxLength - m_text.length());
    } else {
        m_text = txt.isEmpty() ? txt : txt.left(m_maxLength);
    }
    m_history.clear();
    m_modifiedState =  m_undoState = 0;
    m_cursor = (pos < 0 || pos > m_text.length()) ? m_text.length() : pos;
    m_textDirty = (oldText != m_text);
    const bool changed = finishChange(-1, true, edited);

#ifndef QT_NO_ACCESSIBILITY
    if (changed) {
        if (oldText.isEmpty()) {
            QAccessibleTextInsertEvent event(accessibleObject(), 0, txt);
            event.setCursorPosition(m_cursor);
            QAccessible::updateAccessibility(&event);
        } else if (txt.isEmpty()) {
            QAccessibleTextRemoveEvent event(accessibleObject(), 0, oldText);
            event.setCursorPosition(m_cursor);
            QAccessible::updateAccessibility(&event);
        } else {
            QAccessibleTextUpdateEvent event(accessibleObject(), 0, oldText, txt);
            event.setCursorPosition(m_cursor);
            QAccessible::updateAccessibility(&event);
        }
    }
#else
    Q_UNUSED(changed)
#endif
}


/*!
    \internal

    Adds the given \a command to the undo history
    of the line control.  Does not apply the command.
*/
void QWidgetLineControl::addCommand(const Command &cmd)
{
    if (m_separator && m_undoState && m_history[m_undoState - 1].type != Separator) {
        m_history.resize(m_undoState + 2);
        m_history[m_undoState++] = Command(Separator, m_cursor, 0, m_selstart, m_selend);
    } else {
        m_history.resize(m_undoState + 1);
    }
    m_separator = false;
    m_history[m_undoState++] = cmd;
}

/*!
    \internal

    Inserts the given string \a s into the line
    control.

    Also adds the appropriate commands into the undo history.
    This function does not call finishChange(), and may leave the text
    in an invalid state.
*/
void QWidgetLineControl::internalInsert(const QString &s)
{
    if (m_echoMode == QLineEdit::Password) {
        if (m_passwordEchoTimer != 0)
            killTimer(m_passwordEchoTimer);
        int delay = m_passwordMaskDelay;
#ifdef QT_BUILD_INTERNAL
        if (m_passwordMaskDelayOverride >= 0)
            delay = m_passwordMaskDelayOverride;
#endif

        if (delay > 0)
            m_passwordEchoTimer = startTimer(delay);
    }
    if (hasSelectedText())
        addCommand(Command(SetSelection, m_cursor, 0, m_selstart, m_selend));
    if (m_maskData) {
        QString ms = maskString(m_cursor, s);
#ifndef QT_NO_ACCESSIBILITY
        QAccessibleTextInsertEvent insertEvent(accessibleObject(), m_cursor, ms);
        QAccessible::updateAccessibility(&insertEvent);
#endif
        for (int i = 0; i < (int) ms.length(); ++i) {
            addCommand (Command(DeleteSelection, m_cursor + i, m_text.at(m_cursor + i), -1, -1));
            addCommand(Command(Insert, m_cursor + i, ms.at(i), -1, -1));
        }
        m_text.replace(m_cursor, ms.length(), ms);
        m_cursor += ms.length();
        m_cursor = nextMaskBlank(m_cursor);
        m_textDirty = true;
#ifndef QT_NO_ACCESSIBILITY
        QAccessibleTextCursorEvent event(accessibleObject(), m_cursor);
        QAccessible::updateAccessibility(&event);
#endif
    } else {
        int remaining = m_maxLength - m_text.length();
        if (remaining != 0) {
#ifndef QT_NO_ACCESSIBILITY
            QAccessibleTextInsertEvent insertEvent(accessibleObject(), m_cursor, s);
            QAccessible::updateAccessibility(&insertEvent);
#endif
            m_text.insert(m_cursor, s.left(remaining));
            for (int i = 0; i < (int) s.left(remaining).length(); ++i)
               addCommand(Command(Insert, m_cursor++, s.at(i), -1, -1));
            m_textDirty = true;
        }
    }
}

/*!
    \internal

    deletes a single character from the current text.  If \a wasBackspace,
    the character prior to the cursor is removed.  Otherwise the character
    after the cursor is removed.

    Also adds the appropriate commands into the undo history.
    This function does not call finishChange(), and may leave the text
    in an invalid state.
*/
void QWidgetLineControl::internalDelete(bool wasBackspace)
{
    if (m_cursor < (int) m_text.length()) {
        cancelPasswordEchoTimer();
        if (hasSelectedText())
            addCommand(Command(SetSelection, m_cursor, 0, m_selstart, m_selend));
        addCommand(Command((CommandType)((m_maskData ? 2 : 0) + (wasBackspace ? Remove : Delete)),
                   m_cursor, m_text.at(m_cursor), -1, -1));
#ifndef QT_NO_ACCESSIBILITY
        QAccessibleTextRemoveEvent event(accessibleObject(), m_cursor, m_text.at(m_cursor));
        QAccessible::updateAccessibility(&event);
#endif
        if (m_maskData) {
            m_text.replace(m_cursor, 1, clearString(m_cursor, 1));
            addCommand(Command(Insert, m_cursor, m_text.at(m_cursor), -1, -1));
        } else {
            m_text.remove(m_cursor, 1);
        }
        m_textDirty = true;
    }
}

/*!
    \internal

    removes the currently selected text from the line control.

    Also adds the appropriate commands into the undo history.
    This function does not call finishChange(), and may leave the text
    in an invalid state.
*/
void QWidgetLineControl::removeSelectedText()
{
    if (m_selstart < m_selend && m_selend <= (int) m_text.length()) {
        cancelPasswordEchoTimer();
        separate();
        int i ;
        addCommand(Command(SetSelection, m_cursor, 0, m_selstart, m_selend));
        if (m_selstart <= m_cursor && m_cursor < m_selend) {
            // cursor is within the selection. Split up the commands
            // to be able to restore the correct cursor position
            for (i = m_cursor; i >= m_selstart; --i)
                addCommand (Command(DeleteSelection, i, m_text.at(i), -1, 1));
            for (i = m_selend - 1; i > m_cursor; --i)
                addCommand (Command(DeleteSelection, i - m_cursor + m_selstart - 1, m_text.at(i), -1, -1));
        } else {
            for (i = m_selend-1; i >= m_selstart; --i)
                addCommand (Command(RemoveSelection, i, m_text.at(i), -1, -1));
        }
#ifndef QT_NO_ACCESSIBILITY
        QAccessibleTextRemoveEvent event(accessibleObject(), m_selstart, m_text.mid(m_selstart, m_selend - m_selstart));
        QAccessible::updateAccessibility(&event);
#endif
        if (m_maskData) {
            m_text.replace(m_selstart, m_selend - m_selstart,  clearString(m_selstart, m_selend - m_selstart));
            for (int i = 0; i < m_selend - m_selstart; ++i)
                addCommand(Command(Insert, m_selstart + i, m_text.at(m_selstart + i), -1, -1));
        } else {
            m_text.remove(m_selstart, m_selend - m_selstart);
        }
        if (m_cursor > m_selstart)
            m_cursor -= qMin(m_cursor, m_selend) - m_selstart;
        internalDeselect();
        m_textDirty = true;
    }
}

/*!
    \internal

    Parses the input mask specified by \a maskFields to generate
    the mask data used to handle input masks.
*/
void QWidgetLineControl::parseInputMask(const QString &maskFields)
{
    int delimiter = maskFields.indexOf(QLatin1Char(';'));
    if (maskFields.isEmpty() || delimiter == 0) {
        if (m_maskData) {
            delete [] m_maskData;
            m_maskData = 0;
            m_maxLength = 32767;
            internalSetText(QString(), -1, false);
        }
        return;
    }

    if (delimiter == -1) {
        m_blank = QLatin1Char(' ');
        m_inputMask = maskFields;
    } else {
        m_inputMask = maskFields.left(delimiter);
        m_blank = (delimiter + 1 < maskFields.length()) ? maskFields[delimiter + 1] : QLatin1Char(' ');
    }

    // calculate m_maxLength / m_maskData length
    m_maxLength = 0;
    QChar c = 0;
    for (int i=0; i<m_inputMask.length(); i++) {
        c = m_inputMask.at(i);
        if (i > 0 && m_inputMask.at(i-1) == QLatin1Char('\\')) {
            m_maxLength++;
            continue;
        }
        if (c != QLatin1Char('\\') && c != QLatin1Char('!') &&
             c != QLatin1Char('<') && c != QLatin1Char('>') &&
             c != QLatin1Char('{') && c != QLatin1Char('}') &&
             c != QLatin1Char('[') && c != QLatin1Char(']'))
            m_maxLength++;
    }

    delete [] m_maskData;
    m_maskData = new MaskInputData[m_maxLength];

    MaskInputData::Casemode m = MaskInputData::NoCaseMode;
    c = 0;
    bool s;
    bool escape = false;
    int index = 0;
    for (int i = 0; i < m_inputMask.length(); i++) {
        c = m_inputMask.at(i);
        if (escape) {
            s = true;
            m_maskData[index].maskChar = c;
            m_maskData[index].separator = s;
            m_maskData[index].caseMode = m;
            index++;
            escape = false;
        } else if (c == QLatin1Char('<')) {
                m = MaskInputData::Lower;
        } else if (c == QLatin1Char('>')) {
            m = MaskInputData::Upper;
        } else if (c == QLatin1Char('!')) {
            m = MaskInputData::NoCaseMode;
        } else if (c != QLatin1Char('{') && c != QLatin1Char('}') && c != QLatin1Char('[') && c != QLatin1Char(']')) {
            switch (c.unicode()) {
            case 'A':
            case 'a':
            case 'N':
            case 'n':
            case 'X':
            case 'x':
            case '9':
            case '0':
            case 'D':
            case 'd':
            case '#':
            case 'H':
            case 'h':
            case 'B':
            case 'b':
                s = false;
                break;
            case '\\':
                escape = true;
            default:
                s = true;
                break;
            }

            if (!escape) {
                m_maskData[index].maskChar = c;
                m_maskData[index].separator = s;
                m_maskData[index].caseMode = m;
                index++;
            }
        }
    }
    internalSetText(m_text, -1, false);
}


/*!
    \internal

    checks if the key is valid compared to the inputMask
*/
bool QWidgetLineControl::isValidInput(QChar key, QChar mask) const
{
    switch (mask.unicode()) {
    case 'A':
        if (key.isLetter())
            return true;
        break;
    case 'a':
        if (key.isLetter() || key == m_blank)
            return true;
        break;
    case 'N':
        if (key.isLetterOrNumber())
            return true;
        break;
    case 'n':
        if (key.isLetterOrNumber() || key == m_blank)
            return true;
        break;
    case 'X':
        if (key.isPrint())
            return true;
        break;
    case 'x':
        if (key.isPrint() || key == m_blank)
            return true;
        break;
    case '9':
        if (key.isNumber())
            return true;
        break;
    case '0':
        if (key.isNumber() || key == m_blank)
            return true;
        break;
    case 'D':
        if (key.isNumber() && key.digitValue() > 0)
            return true;
        break;
    case 'd':
        if ((key.isNumber() && key.digitValue() > 0) || key == m_blank)
            return true;
        break;
    case '#':
        if (key.isNumber() || key == QLatin1Char('+') || key == QLatin1Char('-') || key == m_blank)
            return true;
        break;
    case 'B':
        if (key == QLatin1Char('0') || key == QLatin1Char('1'))
            return true;
        break;
    case 'b':
        if (key == QLatin1Char('0') || key == QLatin1Char('1') || key == m_blank)
            return true;
        break;
    case 'H':
        if (key.isNumber() || (key >= QLatin1Char('a') && key <= QLatin1Char('f')) || (key >= QLatin1Char('A') && key <= QLatin1Char('F')))
            return true;
        break;
    case 'h':
        if (key.isNumber() || (key >= QLatin1Char('a') && key <= QLatin1Char('f')) || (key >= QLatin1Char('A') && key <= QLatin1Char('F')) || key == m_blank)
            return true;
        break;
    default:
        break;
    }
    return false;
}

/*!
    \internal

    Returns \c true if the given text \a str is valid for any
    validator or input mask set for the line control.

    Otherwise returns \c false
*/
bool QWidgetLineControl::hasAcceptableInput(const QString &str) const
{
#ifndef QT_NO_VALIDATOR
    QString textCopy = str;
    int cursorCopy = m_cursor;
    if (m_validator && m_validator->validate(textCopy, cursorCopy)
        != QValidator::Acceptable)
        return false;
#endif

    if (!m_maskData)
        return true;

    if (str.length() != m_maxLength)
        return false;

    for (int i=0; i < m_maxLength; ++i) {
        if (m_maskData[i].separator) {
            if (str.at(i) != m_maskData[i].maskChar)
                return false;
        } else {
            if (!isValidInput(str.at(i), m_maskData[i].maskChar))
                return false;
        }
    }
    return true;
}

/*!
    \internal

    Applies the inputMask on \a str starting from position \a pos in the mask. \a clear
    specifies from where characters should be gotten when a separator is met in \a str - true means
    that blanks will be used, false that previous input is used.
    Calling this when no inputMask is set is undefined.
*/
QString QWidgetLineControl::maskString(uint pos, const QString &str, bool clear) const
{
    if (pos >= (uint)m_maxLength)
        return QString::fromLatin1("");

    QString fill;
    fill = clear ? clearString(0, m_maxLength) : m_text;

    int strIndex = 0;
    QString s = QString::fromLatin1("");
    int i = pos;
    while (i < m_maxLength) {
        if (strIndex < str.length()) {
            if (m_maskData[i].separator) {
                s += m_maskData[i].maskChar;
                if (str[(int)strIndex] == m_maskData[i].maskChar)
                    strIndex++;
                ++i;
            } else {
                if (isValidInput(str[(int)strIndex], m_maskData[i].maskChar)) {
                    switch (m_maskData[i].caseMode) {
                    case MaskInputData::Upper:
                        s += str[(int)strIndex].toUpper();
                        break;
                    case MaskInputData::Lower:
                        s += str[(int)strIndex].toLower();
                        break;
                    default:
                        s += str[(int)strIndex];
                    }
                    ++i;
                } else {
                    // search for separator first
                    int n = findInMask(i, true, true, str[(int)strIndex]);
                    if (n != -1) {
                        if (str.length() != 1 || i == 0 || (i > 0 && (!m_maskData[i-1].separator || m_maskData[i-1].maskChar != str[(int)strIndex]))) {
                            s += fill.mid(i, n-i+1);
                            i = n + 1; // update i to find + 1
                        }
                    } else {
                        // search for valid m_blank if not
                        n = findInMask(i, true, false, str[(int)strIndex]);
                        if (n != -1) {
                            s += fill.mid(i, n-i);
                            switch (m_maskData[n].caseMode) {
                            case MaskInputData::Upper:
                                s += str[(int)strIndex].toUpper();
                                break;
                            case MaskInputData::Lower:
                                s += str[(int)strIndex].toLower();
                                break;
                            default:
                                s += str[(int)strIndex];
                            }
                            i = n + 1; // updates i to find + 1
                        }
                    }
                }
                ++strIndex;
            }
        } else
            break;
    }

    return s;
}



/*!
    \internal

    Returns a "cleared" string with only separators and blank chars.
    Calling this when no inputMask is set is undefined.
*/
QString QWidgetLineControl::clearString(uint pos, uint len) const
{
    if (pos >= (uint)m_maxLength)
        return QString();

    QString s;
    int end = qMin((uint)m_maxLength, pos + len);
    for (int i = pos; i < end; ++i)
        if (m_maskData[i].separator)
            s += m_maskData[i].maskChar;
        else
            s += m_blank;

    return s;
}

/*!
    \internal

    Strips blank parts of the input in a QWidgetLineControl when an inputMask is set,
    separators are still included. Typically "127.0__.0__.1__" becomes "127.0.0.1".
*/
QString QWidgetLineControl::stripString(const QString &str) const
{
    if (!m_maskData)
        return str;

    QString s;
    int end = qMin(m_maxLength, (int)str.length());
    for (int i = 0; i < end; ++i)
        if (m_maskData[i].separator)
            s += m_maskData[i].maskChar;
        else
            if (str[i] != m_blank)
                s += str[i];

    return s;
}

/*!
    \internal
    searches forward/backward in m_maskData for either a separator or a m_blank
*/
int QWidgetLineControl::findInMask(int pos, bool forward, bool findSeparator, QChar searchChar) const
{
    if (pos >= m_maxLength || pos < 0)
        return -1;

    int end = forward ? m_maxLength : -1;
    int step = forward ? 1 : -1;
    int i = pos;

    while (i != end) {
        if (findSeparator) {
            if (m_maskData[i].separator && m_maskData[i].maskChar == searchChar)
                return i;
        } else {
            if (!m_maskData[i].separator) {
                if (searchChar.isNull())
                    return i;
                else if (isValidInput(searchChar, m_maskData[i].maskChar))
                    return i;
            }
        }
        i += step;
    }
    return -1;
}

void QWidgetLineControl::internalUndo(int until)
{
    if (!isUndoAvailable())
        return;
    cancelPasswordEchoTimer();
    internalDeselect();

    while (m_undoState && m_undoState > until) {
        Command& cmd = m_history[--m_undoState];
        switch (cmd.type) {
        case Insert:
            m_text.remove(cmd.pos, 1);
            m_cursor = cmd.pos;
            break;
        case SetSelection:
            m_selstart = cmd.selStart;
            m_selend = cmd.selEnd;
            m_cursor = cmd.pos;
            break;
        case Remove:
        case RemoveSelection:
            m_text.insert(cmd.pos, cmd.uc);
            m_cursor = cmd.pos + 1;
            break;
        case Delete:
        case DeleteSelection:
            m_text.insert(cmd.pos, cmd.uc);
            m_cursor = cmd.pos;
            break;
        case Separator:
            continue;
        }
        if (until < 0 && m_undoState) {
            Command& next = m_history[m_undoState-1];
            if (next.type != cmd.type && next.type < RemoveSelection
                 && (cmd.type < RemoveSelection || next.type == Separator))
                break;
        }
    }
    m_textDirty = true;
    emitCursorPositionChanged();
}

void QWidgetLineControl::internalRedo()
{
    if (!isRedoAvailable())
        return;
    internalDeselect();
    while (m_undoState < (int)m_history.size()) {
        Command& cmd = m_history[m_undoState++];
        switch (cmd.type) {
        case Insert:
            m_text.insert(cmd.pos, cmd.uc);
            m_cursor = cmd.pos + 1;
            break;
        case SetSelection:
            m_selstart = cmd.selStart;
            m_selend = cmd.selEnd;
            m_cursor = cmd.pos;
            break;
        case Remove:
        case Delete:
        case RemoveSelection:
        case DeleteSelection:
            m_text.remove(cmd.pos, 1);
            m_selstart = cmd.selStart;
            m_selend = cmd.selEnd;
            m_cursor = cmd.pos;
            break;
        case Separator:
            m_selstart = cmd.selStart;
            m_selend = cmd.selEnd;
            m_cursor = cmd.pos;
            break;
        }
        if (m_undoState < (int)m_history.size()) {
            Command& next = m_history[m_undoState];
            if (next.type != cmd.type && cmd.type < RemoveSelection && next.type != Separator
                 && (next.type < RemoveSelection || cmd.type == Separator))
                break;
        }
    }
    m_textDirty = true;
    emitCursorPositionChanged();
}

/*!
    \internal

    If the current cursor position differs from the last emitted cursor
    position, emits cursorPositionChanged().
*/
void QWidgetLineControl::emitCursorPositionChanged()
{
    if (m_cursor != m_lastCursorPos) {
        const int oldLast = m_lastCursorPos;
        m_lastCursorPos = m_cursor;
        cursorPositionChanged(oldLast, m_cursor);
#ifndef QT_NO_ACCESSIBILITY
        // otherwise we send a selection update which includes the cursor
        if (!hasSelectedText()) {
            QAccessibleTextCursorEvent event(accessibleObject(), m_cursor);
            QAccessible::updateAccessibility(&event);
        }
#endif
    }
}

#ifndef QT_NO_COMPLETER
// iterating forward(dir=1)/backward(dir=-1) from the
// current row based. dir=0 indicates a new completion prefix was set.
bool QWidgetLineControl::advanceToEnabledItem(int dir)
{
    int start = m_completer->currentRow();
    if (start == -1)
        return false;
    int i = start + dir;
    if (dir == 0) dir = 1;
    do {
        if (!m_completer->setCurrentRow(i)) {
            if (!m_completer->wrapAround())
                break;
            i = i > 0 ? 0 : m_completer->completionCount() - 1;
        } else {
            QModelIndex currentIndex = m_completer->currentIndex();
            if (m_completer->completionModel()->flags(currentIndex) & Qt::ItemIsEnabled)
                return true;
            i += dir;
        }
    } while (i != start);

    m_completer->setCurrentRow(start); // restore
    return false;
}

void QWidgetLineControl::complete(int key)
{
    if (!m_completer || isReadOnly() || echoMode() != QLineEdit::Normal)
        return;

    QString text = this->text();
    if (m_completer->completionMode() == QCompleter::InlineCompletion) {
        if (key == Qt::Key_Backspace)
            return;
        int n = 0;
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            if (textAfterSelection().length())
                return;
            QString prefix = hasSelectedText() ? textBeforeSelection()
                : text;
            if (text.compare(m_completer->currentCompletion(), m_completer->caseSensitivity()) != 0
                || prefix.compare(m_completer->completionPrefix(), m_completer->caseSensitivity()) != 0) {
                m_completer->setCompletionPrefix(prefix);
            } else {
                n = (key == Qt::Key_Up) ? -1 : +1;
            }
        } else {
            m_completer->setCompletionPrefix(text);
        }
        if (!advanceToEnabledItem(n))
            return;
    } else {
#ifndef QT_KEYPAD_NAVIGATION
        if (text.isEmpty()) {
            m_completer->popup()->hide();
            return;
        }
#endif
        m_completer->setCompletionPrefix(text);
    }

    m_completer->complete();
}
#endif

void QWidgetLineControl::setReadOnly(bool enable)
{
    m_readOnly = enable;
    if (enable)
        setCursorBlinkPeriod(0);
    else
        setCursorBlinkPeriod(QApplication::cursorFlashTime());
}

void QWidgetLineControl::setCursorBlinkPeriod(int msec)
{
    if (msec == m_blinkPeriod)
        return;
    if (m_blinkTimer) {
        killTimer(m_blinkTimer);
    }
    if (msec > 0 && !m_readOnly) {
        m_blinkTimer = startTimer(msec / 2);
        m_blinkStatus = 1;
    } else {
        m_blinkTimer = 0;
        if (m_blinkStatus == 1)
            emit updateNeeded(inputMask().isEmpty() ? cursorRect() : QRect());
    }
    m_blinkPeriod = msec;
}

// This is still used by QDeclarativeTextInput in the qtquick1 repo
void QWidgetLineControl::resetCursorBlinkTimer()
{
    if (m_blinkPeriod == 0 || m_blinkTimer == 0)
        return;
    killTimer(m_blinkTimer);
    m_blinkTimer = startTimer(m_blinkPeriod / 2);
    m_blinkStatus = 1;
}

void QWidgetLineControl::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blinkTimer) {
        m_blinkStatus = !m_blinkStatus;
        emit updateNeeded(inputMask().isEmpty() ? cursorRect() : QRect());
    } else if (event->timerId() == m_deleteAllTimer) {
        killTimer(m_deleteAllTimer);
        m_deleteAllTimer = 0;
        clear();
    } else if (event->timerId() == m_tripleClickTimer) {
        killTimer(m_tripleClickTimer);
        m_tripleClickTimer = 0;
    } else if (event->timerId() == m_passwordEchoTimer) {
        killTimer(m_passwordEchoTimer);
        m_passwordEchoTimer = 0;
        updateDisplayText();
    }
}

#ifndef QT_NO_SHORTCUT
void QWidgetLineControl::processShortcutOverrideEvent(QKeyEvent *ke)
{
    if (isReadOnly())
        return;

    if (ke == QKeySequence::Copy
        || ke == QKeySequence::Paste
        || ke == QKeySequence::Cut
        || ke == QKeySequence::Redo
        || ke == QKeySequence::Undo
        || ke == QKeySequence::MoveToNextWord
        || ke == QKeySequence::MoveToPreviousWord
        || ke == QKeySequence::MoveToEndOfLine
        || ke == QKeySequence::MoveToStartOfDocument
        || ke == QKeySequence::MoveToEndOfDocument
        || ke == QKeySequence::SelectNextWord
        || ke == QKeySequence::SelectPreviousWord
        || ke == QKeySequence::SelectStartOfLine
        || ke == QKeySequence::SelectEndOfLine
        || ke == QKeySequence::SelectStartOfBlock
        || ke == QKeySequence::SelectEndOfBlock
        || ke == QKeySequence::SelectStartOfDocument
        || ke == QKeySequence::SelectAll
        || ke == QKeySequence::SelectEndOfDocument
        || ke == QKeySequence::DeleteCompleteLine) {
        ke->accept();
    } else if (ke->modifiers() == Qt::NoModifier || ke->modifiers() == Qt::ShiftModifier
               || ke->modifiers() == Qt::KeypadModifier) {
        if (ke->key() < Qt::Key_Escape) {
            ke->accept();
        } else {
            switch (ke->key()) {
            case Qt::Key_Delete:
            case Qt::Key_Home:
            case Qt::Key_End:
            case Qt::Key_Backspace:
            case Qt::Key_Left:
            case Qt::Key_Right:
                ke->accept();
            default:
                break;
            }
        }
    }
}
#endif

void QWidgetLineControl::processKeyEvent(QKeyEvent* event)
{
    bool inlineCompletionAccepted = false;

#ifndef QT_NO_COMPLETER
    if (m_completer) {
        QCompleter::CompletionMode completionMode = m_completer->completionMode();
        if ((completionMode == QCompleter::PopupCompletion
             || completionMode == QCompleter::UnfilteredPopupCompletion)
            && m_completer->popup()
            && m_completer->popup()->isVisible()) {
            // The following keys are forwarded by the completer to the widget
            // Ignoring the events lets the completer provide suitable default behavior
            switch (event->key()) {
            case Qt::Key_Escape:
                event->ignore();
                return;
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_F4:
#ifdef QT_KEYPAD_NAVIGATION
            case Qt::Key_Select:
                if (!QApplication::keypadNavigationEnabled())
                    break;
#endif
                m_completer->popup()->hide(); // just hide. will end up propagating to parent
            default:
                break; // normal key processing
            }
        } else if (completionMode == QCompleter::InlineCompletion) {
            switch (event->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_F4:
#ifdef QT_KEYPAD_NAVIGATION
            case Qt::Key_Select:
                if (!QApplication::keypadNavigationEnabled())
                    break;
#endif
                if (!m_completer->currentCompletion().isEmpty() && hasSelectedText()
                    && textAfterSelection().isEmpty()) {
                    setText(m_completer->currentCompletion());
                    inlineCompletionAccepted = true;
                }
            default:
                break; // normal key processing
            }
        }
    }
#endif // QT_NO_COMPLETER

    if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        if (hasAcceptableInput() || fixup()) {

            QInputMethod *inputMethod = QApplication::inputMethod();
            inputMethod->commit();
            QWidget *lineEdit = qobject_cast<QWidget *>(parent());
            if (!(lineEdit && lineEdit->inputMethodHints() & Qt::ImhMultiLine))
                inputMethod->hide();

            emit accepted();
            emit editingFinished();
        }
        if (inlineCompletionAccepted)
            event->accept();
        else
            event->ignore();
        return;
    }

    if (echoMode() == QLineEdit::PasswordEchoOnEdit
        && !passwordEchoEditing()
        && !isReadOnly()
        && !event->text().isEmpty()
#ifdef QT_KEYPAD_NAVIGATION
        && event->key() != Qt::Key_Select
        && event->key() != Qt::Key_Up
        && event->key() != Qt::Key_Down
        && event->key() != Qt::Key_Back
#endif
        && !(event->modifiers() & Qt::ControlModifier)) {
        // Clear the edit and reset to normal echo mode while editing; the
        // echo mode switches back when the edit loses focus
        // ### resets current content.  dubious code; you can
        // navigate with keys up, down, back, and select(?), but if you press
        // "left" or "right" it clears?
        updatePasswordEchoEditing(true);
        clear();
    }

    bool unknown = false;
    bool visual = cursorMoveStyle() == Qt::VisualMoveStyle;

    if (false) {
    }
#ifndef QT_NO_SHORTCUT
    else if (event == QKeySequence::Undo) {
        if (!isReadOnly())
            undo();
    }
    else if (event == QKeySequence::Redo) {
        if (!isReadOnly())
            redo();
    }
    else if (event == QKeySequence::SelectAll) {
        selectAll();
    }
#ifndef QT_NO_CLIPBOARD
    else if (event == QKeySequence::Copy) {
        copy();
    }
    else if (event == QKeySequence::Paste) {
        if (!isReadOnly()) {
            QClipboard::Mode mode = QClipboard::Clipboard;
            if (m_keyboardScheme == QPlatformTheme::X11KeyboardScheme
                && event->modifiers() == (Qt::CTRL | Qt::SHIFT)
                && event->key() == Qt::Key_Insert) {
                mode = QClipboard::Selection;
            }
            paste(mode);
        }
    }
    else if (event == QKeySequence::Cut) {
        if (!isReadOnly() && hasSelectedText()) {
            copy();
            del();
        }
    }
    else if (event == QKeySequence::DeleteEndOfLine) {
        if (!isReadOnly()) {
            setSelection(cursor(), end());
            copy();
            del();
        }
    }
#endif //QT_NO_CLIPBOARD
    else if (event == QKeySequence::MoveToStartOfLine || event == QKeySequence::MoveToStartOfBlock) {
        home(0);
    }
    else if (event == QKeySequence::MoveToEndOfLine || event == QKeySequence::MoveToEndOfBlock) {
        end(0);
    }
    else if (event == QKeySequence::SelectStartOfLine || event == QKeySequence::SelectStartOfBlock) {
        home(1);
    }
    else if (event == QKeySequence::SelectEndOfLine || event == QKeySequence::SelectEndOfBlock) {
        end(1);
    }
    else if (event == QKeySequence::MoveToNextChar) {
#if defined(QT_NO_COMPLETER)
        const bool inlineCompletion = false;
#else
        const bool inlineCompletion = m_completer && m_completer->completionMode() == QCompleter::InlineCompletion;
#endif
        if (hasSelectedText()
           && (m_keyboardScheme != QPlatformTheme::WindowsKeyboardScheme
               || inlineCompletion)) {
            moveCursor(selectionEnd(), false);
        } else {
            cursorForward(0, visual ? 1 : (layoutDirection() == Qt::LeftToRight ? 1 : -1));
        }
    }
    else if (event == QKeySequence::SelectNextChar) {
        cursorForward(1, visual ? 1 : (layoutDirection() == Qt::LeftToRight ? 1 : -1));
    }
    else if (event == QKeySequence::MoveToPreviousChar) {
#if defined(QT_NO_COMPLETER)
        const bool inlineCompletion = false;
#else
        const bool inlineCompletion = m_completer && m_completer->completionMode() == QCompleter::InlineCompletion;
#endif
        if (hasSelectedText()
            && (m_keyboardScheme != QPlatformTheme::WindowsKeyboardScheme
                || inlineCompletion)) {
            moveCursor(selectionStart(), false);
        } else {
            cursorForward(0, visual ? -1 : (layoutDirection() == Qt::LeftToRight ? -1 : 1));
        }
    }
    else if (event == QKeySequence::SelectPreviousChar) {
        cursorForward(1, visual ? -1 : (layoutDirection() == Qt::LeftToRight ? -1 : 1));
    }
    else if (event == QKeySequence::MoveToNextWord) {
        if (echoMode() == QLineEdit::Normal)
            layoutDirection() == Qt::LeftToRight ? cursorWordForward(0) : cursorWordBackward(0);
        else
            layoutDirection() == Qt::LeftToRight ? end(0) : home(0);
    }
    else if (event == QKeySequence::MoveToPreviousWord) {
        if (echoMode() == QLineEdit::Normal)
            layoutDirection() == Qt::LeftToRight ? cursorWordBackward(0) : cursorWordForward(0);
        else if (!isReadOnly()) {
            layoutDirection() == Qt::LeftToRight ? home(0) : end(0);
        }
    }
    else if (event == QKeySequence::SelectNextWord) {
        if (echoMode() == QLineEdit::Normal)
            layoutDirection() == Qt::LeftToRight ? cursorWordForward(1) : cursorWordBackward(1);
        else
            layoutDirection() == Qt::LeftToRight ? end(1) : home(1);
    }
    else if (event == QKeySequence::SelectPreviousWord) {
        if (echoMode() == QLineEdit::Normal)
            layoutDirection() == Qt::LeftToRight ? cursorWordBackward(1) : cursorWordForward(1);
        else
            layoutDirection() == Qt::LeftToRight ? home(1) : end(1);
    }
    else if (event == QKeySequence::Delete) {
        if (!isReadOnly())
            del();
    }
    else if (event == QKeySequence::DeleteEndOfWord) {
        if (!isReadOnly()) {
            cursorWordForward(true);
            del();
        }
    }
    else if (event == QKeySequence::DeleteStartOfWord) {
        if (!isReadOnly()) {
            cursorWordBackward(true);
            del();
        }
    } else if (event == QKeySequence::DeleteCompleteLine) {
        if (!isReadOnly()) {
            setSelection(0, text().size());
#ifndef QT_NO_CLIPBOARD
            copy();
#endif
            del();
        }
    }
#endif // QT_NO_SHORTCUT
    else {
        bool handled = false;
        if (m_keyboardScheme == QPlatformTheme::MacKeyboardScheme
            && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            Qt::KeyboardModifiers myModifiers = (event->modifiers() & ~Qt::KeypadModifier);
            if (myModifiers & Qt::ShiftModifier) {
                if (myModifiers == (Qt::ControlModifier|Qt::ShiftModifier)
                        || myModifiers == (Qt::AltModifier|Qt::ShiftModifier)
                        || myModifiers == Qt::ShiftModifier) {

                    event->key() == Qt::Key_Up ? home(1) : end(1);
                }
            } else {
                if ((myModifiers == Qt::ControlModifier
                     || myModifiers == Qt::AltModifier
                     || myModifiers == Qt::NoModifier)) {
                    event->key() == Qt::Key_Up ? home(0) : end(0);
                }
            }
            handled = true;
        }
        if (event->modifiers() & Qt::ControlModifier) {
            switch (event->key()) {
            case Qt::Key_Backspace:
                if (!isReadOnly()) {
                    cursorWordBackward(true);
                    del();
                }
                break;
#ifndef QT_NO_COMPLETER
            case Qt::Key_Up:
            case Qt::Key_Down:
                complete(event->key());
                break;
#endif
            default:
                if (!handled)
                    unknown = true;
            }
        } else { // ### check for *no* modifier
            switch (event->key()) {
            case Qt::Key_Backspace:
                if (!isReadOnly()) {
                    backspace();
#ifndef QT_NO_COMPLETER
                    complete(Qt::Key_Backspace);
#endif
                }
                break;
#ifdef QT_KEYPAD_NAVIGATION
            case Qt::Key_Back:
                if (QApplication::keypadNavigationEnabled() && !event->isAutoRepeat()
                    && !isReadOnly()) {
                    if (text().length() == 0) {
                        setText(m_cancelText);

                        if (passwordEchoEditing())
                            updatePasswordEchoEditing(false);

                        emit editFocusChange(false);
                    } else if (!m_deleteAllTimer) {
                        m_deleteAllTimer = startTimer(750);
                    }
                } else {
                    unknown = true;
                }
                break;
#endif
            default:
                if (!handled)
                    unknown = true;
            }
        }
    }

    if (event->key() == Qt::Key_Direction_L || event->key() == Qt::Key_Direction_R) {
        setLayoutDirection((event->key() == Qt::Key_Direction_L) ? Qt::LeftToRight : Qt::RightToLeft);
        unknown = false;
    }

	// QTBUG-35734: ignore Ctrl/Ctrl+Shift; accept only AltGr (Alt+Ctrl) on German keyboards
	//
	bool skip = false;
	if (QGuiApplication::inputMethod()->locale().language() == QLocale::German) {
		if (event->modifiers() == Qt::ControlModifier
			|| event->modifiers() == (Qt::ShiftModifier | Qt::ControlModifier)) {
			skip = true;
		}
	}

	if (unknown && !isReadOnly() && !skip) {
        QString t = event->text();
		if (!t.isEmpty() && (t.at(0).isPrint() || t.at(0).unicode() == 0x200C || t.at(0).unicode() == 0x200D)) {
            insert(t);
#ifndef QT_NO_COMPLETER
            complete(event->key());
#endif
            event->accept();
            return;
        }
    }

    if (unknown)
        event->ignore();
    else
        event->accept();
}

bool QWidgetLineControl::isUndoAvailable() const
{
    // For security reasons undo is not available in any password mode (NoEcho included)
    // with the exception that the user can clear the password with undo.
    return !m_readOnly && m_undoState
            && (m_echoMode == QLineEdit::Normal || m_history[m_undoState - 1].type == QWidgetLineControl::Insert);
}

bool QWidgetLineControl::isRedoAvailable() const
{
    // Same as with undo. Disabled for password modes.
    return !m_readOnly
            && m_echoMode == QLineEdit::Normal
            && m_undoState < m_history.size();
}

QT_END_NAMESPACE

#endif
