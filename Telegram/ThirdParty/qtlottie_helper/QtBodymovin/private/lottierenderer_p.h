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

#ifndef LOTTIERENDERER_H
#define LOTTIERENDERER_H

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

#include <QStack>

#include "bmglobal.h"

QT_BEGIN_NAMESPACE

class BMBase;
class BMLayer;
class BMRect;
class BMFill;
class BMGFill;
class BMStroke;
class BMBasicTransform;
class BMLayerTransform;
class BMShapeTransform;
class BMRepeaterTransform;
class BMShapeLayer;
class BMEllipse;
class BMRound;
class BMFreeFormShape;
class BMTrimPath;
class BMFillEffect;
class BMRepeater;

class BODYMOVIN_EXPORT LottieRenderer
{
public:
    enum TrimmingState{Off = 0, Simultaneous, Individual};

    virtual ~LottieRenderer() = default;

    virtual void saveState() = 0;
    virtual void restoreState() = 0;

    virtual void setTrimmingState(TrimmingState state);
    virtual TrimmingState trimmingState() const;

    virtual void render(const BMLayer &layer) = 0;
    virtual void render(const BMRect &rect) = 0;
    virtual void render(const BMEllipse &ellipse) = 0;
    virtual void render(const BMRound &round) = 0;
    virtual void render(const BMFill &fill) = 0;
    virtual void render(const BMGFill &fill) = 0;
    virtual void render(const BMStroke &stroke) = 0;
    virtual void render(const BMBasicTransform &trans) = 0;
    virtual void render(const BMShapeTransform &trans) = 0;
    virtual void render(const BMFreeFormShape &shape) = 0;
    virtual void render(const BMTrimPath &trans) = 0;
    virtual void render(const BMFillEffect &effect) = 0;
    virtual void render(const BMRepeater &repeater) = 0;

protected:
    void saveTrimmingState();
    void restoreTrimmingState();

    TrimmingState m_trimmingState = Off;

private:
    QStack<LottieRenderer::TrimmingState> m_trimStateStack;
};

QT_END_NAMESPACE

#endif // LOTTIERENDERER_H
