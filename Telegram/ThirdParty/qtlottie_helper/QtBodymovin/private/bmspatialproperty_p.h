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

#ifndef BMSPATIALPROPERTY_P_H
#define BMSPATIALPROPERTY_P_H

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

#include <QPointF>
#include <QPainterPath>

#include <QtBodymovin/private/bmproperty_p.h>

QT_BEGIN_NAMESPACE

class BMSpatialProperty : public BMProperty2D<QPointF>
{
public:
    virtual void construct(const QJsonObject &definition) override
    {
        qCDebug(lcLottieQtBodymovinParser) << "BMSpatialProperty::construct()";
        BMProperty2D<QPointF>::construct(definition);
    }

    virtual EasingSegment<QPointF> parseKeyframe(const QJsonObject keyframe, bool fromExpression) override
    {
        EasingSegment<QPointF> easing = BMProperty2D<QPointF>::parseKeyframe(keyframe, fromExpression);

        // No need to parse further incomplete keyframes (i.e. last keyframes)
        if (!easing.complete) {
            return easing;
        }

        qreal tix = 0, tiy = 0, tox = 0, toy = 0;
        if (fromExpression) {
            // If spatial property definition originates from
            // an expression (specifically Slider), it contains scalar
            // property. It must be expanded to both x and y coordinates
            QJsonArray iArr = keyframe.value(QLatin1String("i")).toArray();
            QJsonArray oArr = keyframe.value(QLatin1String("o")).toArray();

            if (iArr.count() && oArr.count()) {
                tix = iArr.at(0).toDouble();
                tiy = tix;
                tox = oArr.at(0).toDouble();
                toy = tox;
            }
        } else {
            QJsonArray tiArr = keyframe.value(QLatin1String("ti")).toArray();
            QJsonArray toArr = keyframe.value(QLatin1String("to")).toArray();

            if (tiArr.count() && toArr.count()) {
                tix = tiArr.at(0).toDouble();
                tiy = tiArr.at(1).toDouble();
                tox = toArr.at(0).toDouble();
                toy = toArr.at(1).toDouble();
            }
        }
        QPointF s(easing.startValue);
        QPointF e(easing.endValue);
        QPointF c1(tox, toy);
        QPointF c2(tix, tiy);

        c1 += s;
        c2 += e;

        m_bezierPath.moveTo(s);
        m_bezierPath.cubicTo(c1, c2, e);

        return easing;
    }

    virtual bool update(int frame) override
    {
        if (!m_animated)
            return false;

        int adjustedFrame = qBound(m_startFrame, frame, m_endFrame);
        if (const EasingSegment<QPointF> *easing = getEasingSegment(adjustedFrame)) {
            qreal progress = ((adjustedFrame - m_startFrame) * 1.0) / (m_endFrame - m_startFrame);
            qreal easedValue = easing->easing.valueForProgress(progress);
            m_value = m_bezierPath.pointAtPercent(easedValue);
        }

        return true;
    }

private:
    QPainterPath m_bezierPath;
};

QT_END_NAMESPACE

#endif // BMSPATIALPROPERTY_P_H
