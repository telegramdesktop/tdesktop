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

	virtual void postprocessEasingCurve(
			EasingSegment<QPointF> &easing,
			const QJsonObject keyframe) override {
        // No need to parse further incomplete keyframes (i.e. last keyframes)
        if (easing.state != EasingSegmentState::Complete) {
            return;
        }

        qreal tix = 0, tiy = 0, tox = 0, toy = 0;
        QJsonArray tiArr = keyframe.value(QLatin1String("ti")).toArray();
        QJsonArray toArr = keyframe.value(QLatin1String("to")).toArray();

        if (tiArr.count() && toArr.count()) {
            tix = tiArr.at(0).toDouble();
            tiy = tiArr.at(1).toDouble();
            tox = toArr.at(0).toDouble();
            toy = toArr.at(1).toDouble();
        }
        QPointF s(easing.startValue);
        QPointF e(easing.endValue);
        QPointF c1(tox, toy);
        QPointF c2(tix, tiy);

        c1 += s;
        c2 += e;

        easing.bezier.moveTo(s);
        easing.bezier.cubicTo(c1, c2, e);

        const auto kCount = 150;
        easing.bezierPoints.reserve(kCount);
        for (auto k = 0; k < kCount; ++k) {
            const auto percent = double(k) / (kCount - 1.);
            auto point = EasingSegment<QPointF>::BezierPoint();
            point.point = easing.bezier.pointAtPercent(percent);
            if (k > 0) {
                const auto delta = (point.point - easing.bezierPoints[k - 1].point);
                point.length = std::sqrt(QPointF::dotProduct(delta, delta));
                easing.bezierLength += point.length;
            }
            easing.bezierPoints.push_back(point);
        }
    }

    virtual bool update(int frame) override
    {
        if (!m_animated)
            return false;

        int adjustedFrame = qBound(m_startFrame, frame, m_endFrame);
        if (const EasingSegment<QPointF> *easing = getEasingSegment(adjustedFrame)) {
            if (easing->state == EasingSegmentState::Complete) {
                int length = (easing->endFrame - easing->startFrame);
                qreal progress = (length > 0)
                    ? ((adjustedFrame - easing->startFrame) * 1.0) / length
                    : 1.;
                qreal easedValue = easing->easing.valueForProgress(progress);
                //m_value = easing->bezier.pointAtPercent(easedValue);

                const auto distance = easedValue * easing->bezierLength;
                auto segmentPerc = 0.;
                auto addedLength = 0.;
                const auto count = easing->bezierPoints.size();
                for (auto j = 0; j != count; ++j) {
                    addedLength += easing->bezierPoints[j].length;
                    if (distance == 0. || easedValue == 0. || j == count - 1) {
                        m_value = easing->bezierPoints[j].point;
                        break;
                    } else if (distance >= addedLength && distance < addedLength + easing->bezierPoints[j + 1].length) {
                        segmentPerc = (distance - addedLength) / easing->bezierPoints[j + 1].length;
                        m_value = easing->bezierPoints[j].point + (easing->bezierPoints[j + 1].point - easing->bezierPoints[j].point) * segmentPerc;
                        break;
                    }
                }
            } else {
                // In case of incomplete easing we should just take the final point.
                //m_value = m_bezierPath.pointAtPercent(1.);
                m_value = easing->endValue;
            }
        }

        return true;
    }

};

QT_END_NAMESPACE

#endif // BMSPATIALPROPERTY_P_H
