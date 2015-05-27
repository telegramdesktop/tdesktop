/***************************************************************************
 *   Copyright (C) 2013~2013 by CSSlayer                                   *
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

/* this is forked from kdelibs/kdeui/kkeysequencewidget.cpp */

/*
    Original Copyright header
    Copyright (C) 1998 Mark Donohoe <donohoe@kde.org>
    Copyright (C) 2001 Ellis Whitehead <ellis@kde.org>
    Copyright (C) 2007 Andreas Hartmetz <ahartmetz@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "fcitxqtkeysequencewidget.h"
#include "fcitxqtkeysequencewidget_p.h"

#include <QKeyEvent>
#include <QTimer>
#include <QHash>
#include <QHBoxLayout>
#include <QToolButton>
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <libintl.h>
#include <fcitx-utils/keysymgen.h>

#include "qtkeytrans.h"

#define _(x) QString::fromUtf8(dgettext("fcitx", x))

class FcitxQtKeySequenceWidgetPrivate
{
public:
    FcitxQtKeySequenceWidgetPrivate(FcitxQtKeySequenceWidget *q);

    void init();

    static QKeySequence appendToSequence(const QKeySequence& seq, int keyQt);
    static bool isOkWhenModifierless(int keyQt);

    void updateShortcutDisplay();
    void startRecording();

    void controlModifierlessTimout()
    {
        if (nKey != 0 && !modifierKeys) {
            // No modifier key pressed currently. Start the timout
            modifierlessTimeout.start(600);
        } else {
            // A modifier is pressed. Stop the timeout
            modifierlessTimeout.stop();
        }

    }


    void cancelRecording()
    {
        keySequence = oldKeySequence;
        side = oldSide;
        doneRecording();
    }

//private slot
    void doneRecording(bool validate = true);

//members
    FcitxQtKeySequenceWidget *const q;
    QHBoxLayout *layout;
    FcitxQtKeySequenceButton *keyButton;
    QToolButton *clearButton;

    QKeySequence keySequence;
    QKeySequence oldKeySequence;
    QTimer modifierlessTimeout;
    bool allowModifierless;
    uint nKey;
    uint modifierKeys;
    bool isRecording;
    bool multiKeyShortcutsAllowed;
    bool allowModifierOnly;
    FcitxQtModifierSide side;
    FcitxQtModifierSide oldSide;
};

FcitxQtKeySequenceWidgetPrivate::FcitxQtKeySequenceWidgetPrivate(FcitxQtKeySequenceWidget *q)
    : q(q)
     ,layout(NULL)
     ,keyButton(NULL)
     ,clearButton(NULL)
     ,allowModifierless(false)
     ,nKey(0)
     ,modifierKeys(0)
     ,isRecording(false)
     ,multiKeyShortcutsAllowed(true)
     ,allowModifierOnly(false)
     ,side(MS_Unknown)
{}

FcitxQtKeySequenceWidget::FcitxQtKeySequenceWidget(QWidget *parent)
 : QWidget(parent),
   d(new FcitxQtKeySequenceWidgetPrivate(this))
{
    d->init();
    setFocusProxy( d->keyButton );
    connect(d->keyButton, SIGNAL(clicked()), this, SLOT(captureKeySequence()));
    connect(d->clearButton, SIGNAL(clicked()), this, SLOT(clearKeySequence()));
    connect(&d->modifierlessTimeout, SIGNAL(timeout()), this, SLOT(doneRecording()));
    //TODO: how to adopt style changes at runtime?
    /*QFont modFont = d->clearButton->font();
    modFont.setStyleHint(QFont::TypeWriter);
    d->clearButton->setFont(modFont);*/
    d->updateShortcutDisplay();
}


void FcitxQtKeySequenceWidgetPrivate::init()
{
    layout = new QHBoxLayout(q);
    layout->setMargin(0);

    keyButton = new FcitxQtKeySequenceButton(this, q);
    keyButton->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(keyButton);

    clearButton = new QToolButton(q);
    layout->addWidget(clearButton);

#if QT_VERSION < QT_VERSION_CHECK(4, 8, 0)
    clearButton->setText(_("Clear"));
#else
    keyButton->setIcon(QIcon::fromTheme("configure"));
    if (qApp->isLeftToRight())
        clearButton->setIcon(QIcon::fromTheme("edit-clear-locationbar-rtl"));
    else
        clearButton->setIcon(QIcon::fromTheme("edit-clear-locationbar-ltr"));
#endif
}


FcitxQtKeySequenceWidget::~FcitxQtKeySequenceWidget ()
{
    delete d;
}

bool FcitxQtKeySequenceWidget::multiKeyShortcutsAllowed() const
{
    return d->multiKeyShortcutsAllowed;
}


void FcitxQtKeySequenceWidget::setMultiKeyShortcutsAllowed(bool allowed)
{
    d->multiKeyShortcutsAllowed = allowed;
}

void FcitxQtKeySequenceWidget::setModifierlessAllowed(bool allow)
{
    d->allowModifierless = allow;
}

bool FcitxQtKeySequenceWidget::isModifierlessAllowed()
{
    return d->allowModifierless;
}

bool FcitxQtKeySequenceWidget::isModifierOnlyAllowed()
{
    return d->allowModifierOnly;
}

void FcitxQtKeySequenceWidget::setModifierOnlyAllowed(bool allow)
{
    d->allowModifierOnly = allow;
}

FcitxQtModifierSide FcitxQtKeySequenceWidget::modifierSide()
{
    return d->side;
}

void FcitxQtKeySequenceWidget::setClearButtonShown(bool show)
{
    d->clearButton->setVisible(show);
}

//slot
void FcitxQtKeySequenceWidget::captureKeySequence()
{
    d->startRecording();
}


QKeySequence FcitxQtKeySequenceWidget::keySequence() const
{
    return d->keySequence;
}


//slot
void FcitxQtKeySequenceWidget::setKeySequence(const QKeySequence& seq, FcitxQtModifierSide side, FcitxQtKeySequenceWidget::Validation validate)
{
    // oldKeySequence holds the key sequence before recording started, if setKeySequence()
    // is called while not recording then set oldKeySequence to the existing sequence so
    // that the keySequenceChanged() signal is emitted if the new and previous key
    // sequences are different
    if (!d->isRecording) {
        d->oldKeySequence = d->keySequence;
        d->oldSide = d->side;
    }

    d->side = side;
    d->keySequence = seq;
    d->doneRecording(validate == Validate);
}


//slot
void FcitxQtKeySequenceWidget::clearKeySequence()
{
    setKeySequence(QKeySequence());
    d->side = MS_Unknown;
}

void FcitxQtKeySequenceWidgetPrivate::startRecording()
{
    nKey = 0;
    modifierKeys = 0;
    oldKeySequence = keySequence;
    oldSide = side;
    keySequence = QKeySequence();
    side = MS_Unknown;
    isRecording = true;
    keyButton->grabKeyboard();

    if (!QWidget::keyboardGrabber()) {
        qWarning() << "Failed to grab the keyboard! Most likely qt's nograb option is active";
    }

    keyButton->setDown(true);
    updateShortcutDisplay();
}


void FcitxQtKeySequenceWidgetPrivate::doneRecording(bool validate)
{
    modifierlessTimeout.stop();
    isRecording = false;
    keyButton->releaseKeyboard();
    keyButton->setDown(false);

    if (keySequence==oldKeySequence && (oldSide == side || !allowModifierOnly)) {
        // The sequence hasn't changed
        updateShortcutDisplay();
        return;
    }

    Q_EMIT q->keySequenceChanged(keySequence, side);

    updateShortcutDisplay();
}


void FcitxQtKeySequenceWidgetPrivate::updateShortcutDisplay()
{
    do {
        if (keySequence.count() != 1) {
            break;
        }
        int key = keySequence[0] & (~Qt::KeyboardModifierMask);
        if (key == Qt::Key_Shift
            || key == Qt::Key_Control
            || key == Qt::Key_Meta
            || key == Qt::Key_Alt) {
            QString s;
            int mod = keySequence[0] & (Qt::KeyboardModifierMask);
            if (mod & Qt::META && key != Qt::Key_Meta)  s += "Meta+";
            if (mod & Qt::CTRL && key != Qt::Key_Control)  s += "Ctrl+";
            if (mod & Qt::ALT && key != Qt::Key_Alt)   s += "Alt+";
            if (mod & Qt::SHIFT && key != Qt::Key_Shift) s += "Shift+";

            if (side == MS_Left) {
                s += _("Left") + " ";
            } else if (side == MS_Right) {
                s += _("Right") + " ";
            }

            switch(key) {
                case Qt::Key_Shift:
                    s += "Shift";
                    break;
                case Qt::Key_Control:
                    s += "Ctrl";
                    break;
                case Qt::Key_Meta:
                    s += "Meta";
                    break;
                case Qt::Key_Alt:
                    s += "Alt";
                    break;
            }
            keyButton->setText(s);
            return;
        }
    } while(0);

    //empty string if no non-modifier was pressed
    QString s = keySequence.toString(QKeySequence::NativeText);
    s.replace('&', QLatin1String("&&"));

    if (isRecording) {
        if (modifierKeys) {
            if (!s.isEmpty()) s.append(",");
            if (modifierKeys & Qt::META)  s += "Meta+";
            if (modifierKeys & Qt::CTRL)  s += "Ctrl+";
            if (modifierKeys & Qt::ALT)   s += "Alt+";
            if (modifierKeys & Qt::SHIFT) s += "Shift+";

        } else if (nKey == 0) {
            s = "...";
        }
        //make it clear that input is still going on
        s.append(" ...");
    }

    if (s.isEmpty()) {
        s = _("Empty");
    }

    s.prepend(' ');
    s.append(' ');
    keyButton->setText(s);
}


FcitxQtKeySequenceButton::~FcitxQtKeySequenceButton()
{
}


//prevent Qt from special casing Tab and Backtab
bool FcitxQtKeySequenceButton::event (QEvent* e)
{
    if (d->isRecording && e->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent *>(e));
        return true;
    }

    // The shortcut 'alt+c' ( or any other dialog local action shortcut )
    // ended the recording and triggered the action associated with the
    // action. In case of 'alt+c' ending the dialog.  It seems that those
    // ShortcutOverride events get sent even if grabKeyboard() is active.
    if (d->isRecording && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }

    return QPushButton::event(e);
}


void FcitxQtKeySequenceButton::keyPressEvent(QKeyEvent *e)
{
    int keyQt = e->key();
    if (keyQt == -1) {
        // Qt sometimes returns garbage keycodes, I observed -1, if it doesn't know a key.
        // We cannot do anything useful with those (several keys have -1, indistinguishable)
        // and QKeySequence.toString() will also yield a garbage string.
        QMessageBox::warning(this,
                _("The key you just pressed is not supported by Qt."),
                _("Unsupported Key"));
        return d->cancelRecording();
    }

    uint newModifiers = e->modifiers() & (Qt::SHIFT | Qt::CTRL | Qt::ALT | Qt::META);

    //don't have the return or space key appear as first key of the sequence when they
    //were pressed to start editing - catch and them and imitate their effect
    if (!d->isRecording && ((keyQt == Qt::Key_Return || keyQt == Qt::Key_Space))) {
        d->startRecording();
        d->modifierKeys = newModifiers;
        d->updateShortcutDisplay();
        return;
    }

    // We get events even if recording isn't active.
    if (!d->isRecording)
        return QPushButton::keyPressEvent(e);

    e->accept();
    d->modifierKeys = newModifiers;


    switch(keyQt) {
    case Qt::Key_AltGr: //or else we get unicode salad
        return;
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_Menu: //unused (yes, but why?)
        d->controlModifierlessTimout();
        d->updateShortcutDisplay();
        break;
    default:

        if (d->nKey == 0 && !(d->modifierKeys & ~Qt::SHIFT)) {
            // It's the first key and no modifier pressed. Check if this is
            // allowed
            if (!(FcitxQtKeySequenceWidgetPrivate::isOkWhenModifierless(keyQt)
                            || d->allowModifierless)) {
                // No it's not
                return;
            }
        }

        // We now have a valid key press.
        if (keyQt) {
            if ((keyQt == Qt::Key_Backtab) && (d->modifierKeys & Qt::SHIFT)) {
                keyQt = Qt::Key_Tab | d->modifierKeys;
            }
            else {
                keyQt |= d->modifierKeys;
            }

            if (d->nKey == 0) {
                d->keySequence = QKeySequence(keyQt);
            } else {
                d->keySequence =
                  FcitxQtKeySequenceWidgetPrivate::appendToSequence(d->keySequence, keyQt);
            }

            d->nKey++;
            if ((!d->multiKeyShortcutsAllowed) || (d->nKey >= 4)) {
                d->doneRecording();
                return;
            }
            d->controlModifierlessTimout();
            d->updateShortcutDisplay();
        }
    }
}


void FcitxQtKeySequenceButton::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == -1) {
        // ignore garbage, see keyPressEvent()
        return;
    }

    if (!d->isRecording)
        return QPushButton::keyReleaseEvent(e);

    e->accept();

    if (!d->multiKeyShortcutsAllowed
        && d->allowModifierOnly
        && (e->key() == Qt::Key_Shift
            || e->key() == Qt::Key_Control
            || e->key() == Qt::Key_Meta
            || e->key() == Qt::Key_Alt)) {
        d->side = MS_Unknown;

        if (qApp->platformName() == "xcb") {

            if (e->nativeVirtualKey() == FcitxKey_Control_L
            || e->nativeVirtualKey() == FcitxKey_Alt_L
            || e->nativeVirtualKey() == FcitxKey_Shift_L
            || e->nativeVirtualKey() == FcitxKey_Super_L) {
                d->side = MS_Left;
            }
            if (e->nativeVirtualKey() == FcitxKey_Control_R
            || e->nativeVirtualKey() == FcitxKey_Alt_R
            || e->nativeVirtualKey() == FcitxKey_Shift_R
            || e->nativeVirtualKey() == FcitxKey_Super_R) {
                d->side = MS_Right;
            }
        }
        int keyQt = e->key() | d->modifierKeys;
        d->keySequence = QKeySequence(keyQt);
        d->doneRecording();
        return;
    }


    uint newModifiers = e->modifiers() & (Qt::SHIFT | Qt::CTRL | Qt::ALT | Qt::META);

    //if a modifier that belongs to the shortcut was released...
    if ((newModifiers & d->modifierKeys) < d->modifierKeys) {
        d->modifierKeys = newModifiers;
        d->controlModifierlessTimout();
        d->updateShortcutDisplay();
    }
}


//static
QKeySequence FcitxQtKeySequenceWidgetPrivate::appendToSequence(const QKeySequence& seq, int keyQt)
{
    switch (seq.count()) {
    case 0:
        return QKeySequence(keyQt);
    case 1:
        return QKeySequence(seq[0], keyQt);
    case 2:
        return QKeySequence(seq[0], seq[1], keyQt);
    case 3:
        return QKeySequence(seq[0], seq[1], seq[2], keyQt);
    default:
        return seq;
    }
}


//static
bool FcitxQtKeySequenceWidgetPrivate::isOkWhenModifierless(int keyQt)
{
    //this whole function is a hack, but especially the first line of code
    if (QKeySequence(keyQt).toString().length() == 1)
        return false;

    switch (keyQt) {
    case Qt::Key_Return:
    case Qt::Key_Space:
    case Qt::Key_Tab:
    case Qt::Key_Backtab: //does this ever happen?
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        return false;
    default:
        return true;
    }
}

void FcitxQtKeySequenceWidget::keyQtToFcitx(int keyQt, FcitxQtModifierSide side, int& outsym, uint& outstate)
{
    int key = keyQt & (~Qt::KeyboardModifierMask);
    int state = keyQt & Qt::KeyboardModifierMask;
    int sym = 0;
    keyQtToSym(key, Qt::KeyboardModifiers(state), sym, outstate);
    if (side == MS_Right) {
        switch (sym) {
            case FcitxKey_Control_L:
                sym = FcitxKey_Control_R;
                break;
            case FcitxKey_Alt_L:
                sym = FcitxKey_Alt_R;
                break;
            case FcitxKey_Shift_L:
                sym = FcitxKey_Shift_R;
                break;
            case FcitxKey_Super_L:
                sym = FcitxKey_Super_R;
                break;
        }
    }

    outsym = sym;
}

int FcitxQtKeySequenceWidget::keyFcitxToQt(int sym, uint state)
{
    Qt::KeyboardModifiers qstate = Qt::NoModifier;

    int key;
    symToKeyQt((int) sym, state, key, qstate);

    return key | qstate;
}


#include "moc_fcitxqtkeysequencewidget.cpp"
