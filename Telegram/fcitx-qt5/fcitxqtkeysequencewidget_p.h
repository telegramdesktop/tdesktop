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

/* this is forked from kdelibs/kdeui/kkeysequencewidget_p.h */

/*
    Original Copyright header
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
#ifndef FCITXQT_KEYSEQUENCEWIDGET_P_H
#define FCITXQT_KEYSEQUENCEWIDGET_P_H

#include <QPushButton>
#include <QHBoxLayout>
#include <QToolButton>

class FcitxQtKeySequenceWidgetPrivate;
class FcitxQtKeySequenceButton: public QPushButton
{
    Q_OBJECT

public:
    explicit FcitxQtKeySequenceButton(FcitxQtKeySequenceWidgetPrivate *d, QWidget *parent)
     : QPushButton(parent),
       d(d) {}

    virtual ~FcitxQtKeySequenceButton();

protected:
    /**
    * Reimplemented for internal reasons.
    */
    virtual bool event (QEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void keyReleaseEvent(QKeyEvent *event);

private:
    FcitxQtKeySequenceWidgetPrivate *const d;
};

#endif //FCITXQT_KEYSEQUENCEWIDGET_P_H
