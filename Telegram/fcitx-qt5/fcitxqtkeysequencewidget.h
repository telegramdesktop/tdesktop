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

/* this is forked from kdelibs/kdeui/kkeysequencewidget.h */

/*
    Original Copyright header
    This file is part of the KDE libraries
    Copyright (C) 2001, 2002 Ellis Whitehead <ellis@kde.org>
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

#ifndef KKEYSEQUENCEWIDGET_H
#define KKEYSEQUENCEWIDGET_H

#include <QtCore/QList>
#include <QtWidgets/QPushButton>

#include "fcitxqtwidgetsaddons_export.h"

enum FcitxQtModifierSide {
    MS_Unknown = 0,
    MS_Left = 1,
    MS_Right = 2
};

class FcitxQtKeySequenceWidgetPrivate;

class FCITXQTWIDGETSADDONS_EXPORT FcitxQtKeySequenceWidget: public QWidget
{
    Q_OBJECT

    Q_PROPERTY(
            bool multiKeyShortcutsAllowed
            READ multiKeyShortcutsAllowed
            WRITE setMultiKeyShortcutsAllowed )

    Q_PROPERTY(
            bool modifierlessAllowed
            READ isModifierlessAllowed
            WRITE setModifierlessAllowed )

    Q_PROPERTY(
            bool modifierOnlyAllowed
            READ isModifierOnlyAllowed
            WRITE setModifierOnlyAllowed )

public:
    enum Validation {
        Validate = 0,
        NoValidate = 1
    };

    /**
    * Constructor.
    */
    explicit FcitxQtKeySequenceWidget(QWidget *parent = 0);

    /**
    * Destructs the widget.
    */
    virtual ~FcitxQtKeySequenceWidget();

    void setMultiKeyShortcutsAllowed(bool);
    bool multiKeyShortcutsAllowed() const;

    void setModifierlessAllowed(bool allow);
    bool isModifierlessAllowed();

    void setModifierOnlyAllowed(bool allow);
    bool isModifierOnlyAllowed();

    /*
     * only useful when modifierOnlyAllowed is true
     * and the key is modifierOnly.
     */
    FcitxQtModifierSide modifierSide();

    void setClearButtonShown(bool show);

    QKeySequence keySequence() const;

    static void keyQtToFcitx(int keyQt, FcitxQtModifierSide side, int& outsym, uint& outstate);
    static int keyFcitxToQt(int sym, uint state);

Q_SIGNALS:
    void keySequenceChanged(const QKeySequence &seq, FcitxQtModifierSide side);

public Q_SLOTS:
    void captureKeySequence();
    void setKeySequence(const QKeySequence &seq, FcitxQtModifierSide side = MS_Unknown, Validation val = NoValidate);
    void clearKeySequence();

private:
    Q_PRIVATE_SLOT(d, void doneRecording())

private:
    friend class FcitxQtKeySequenceWidgetPrivate;
    FcitxQtKeySequenceWidgetPrivate *const d;

    Q_DISABLE_COPY(FcitxQtKeySequenceWidget)
};

#endif //KKEYSEQUENCEWIDGET_H
