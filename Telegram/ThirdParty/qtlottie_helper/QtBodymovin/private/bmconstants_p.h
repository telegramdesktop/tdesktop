/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the lottie-qt module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef BMCONSTANTS_P_H
#define BMCONSTANTS_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QObject>
#include <QLoggingCategory>

#include <QtBodymovin/bmglobal.h>

#define BM_LAYER_PRECOMP_IX     0x10000
#define BM_LAYER_SOLID_IX       0x10001
#define BM_LAYER_IMAGE_IX       0x10002
#define BM_LAYER_NULL_IX        0x10004
#define BM_LAYER_SHAPE_IX       0x10008
#define BM_LAYER_TEXT_IX        0x1000f

#define BM_EFFECT_FILL          0x20000

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(lcLottieQtBodymovinParser);
Q_DECLARE_LOGGING_CATEGORY(lcLottieQtBodymovinUpdate);
Q_DECLARE_LOGGING_CATEGORY(lcLottieQtBodymovinRender);
Q_DECLARE_LOGGING_CATEGORY(lcLottieQtBodymovinRenderThread);

class BODYMOVIN_EXPORT BMLiteral : public QObject
{
    Q_OBJECT
public:
    enum ElementType {
        Animation = 0,
        LayerImage,
        LayerNull,
        LayerPrecomp,
        LayerShape
    };

    enum PropertyType {
        RectPosition,
        RectSize,
        RectRoundness
    };
    Q_ENUM(PropertyType)

    explicit BMLiteral(QObject *parent = nullptr) : QObject(parent) {}
};

QT_END_NAMESPACE

#endif // BMCONSTANTS_P_H
