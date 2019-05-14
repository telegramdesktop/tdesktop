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

#ifndef BMPROPERTY_P_H
#define BMPROPERTY_P_H

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

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QPointF>
#include <QLoggingCategory>
#include <QtMath>

#include <QDebug>

#include <QtBodymovin/private/bmconstants_p.h>
#include <QtBodymovin/private/beziereasing_p.h>

QT_BEGIN_NAMESPACE

enum class EasingSegmentState : char {
    Complete,
    Incomplete,
    Final,
};

template<typename T>
struct EasingSegment {
    EasingSegmentState state = EasingSegmentState::Incomplete;
    double startFrame = 0;
    double endFrame = 0;
    T startValue;
    T endValue;
    BezierEasing easing;
    QPainterPath bezier;

    double bezierLength = 0.;
    struct BezierPoint {
        QPointF point;
        double length = 0.;
    };
    std::vector<BezierPoint> bezierPoints;
};

template<typename T>
class BODYMOVIN_EXPORT BMProperty
{
public:
    virtual ~BMProperty() = default;

    virtual void construct(const QJsonObject &definition)
    {
        if (definition.value(QStringLiteral("s")).toVariant().toInt())
            qCWarning(lcLottieQtBodymovinParser)
                    << "Property is split into separate x and y but it is not supported";

        m_animated = definition.value(QStringLiteral("a")).toDouble() > 0;
        if (m_animated) {
            QJsonArray keyframes = definition.value(QStringLiteral("k")).toArray();
            QJsonArray::const_iterator it = keyframes.constBegin();
			QJsonArray::const_iterator previous;
            while (it != keyframes.constEnd()) {
				QJsonObject keyframe = (*it).toObject();
                EasingSegment<T> easing = parseKeyframe(keyframe);
                addEasing(easing);

				if (m_easingCurves.length() > 1) {
					postprocessEasingCurve(
						m_easingCurves[m_easingCurves.length() - 2],
						(*previous).toObject());
				}
				previous = it;
                ++it;
            }
            finalizeEasingCurves();
			if (m_easingCurves.length() > 0) {
				EasingSegment<T> &last = m_easingCurves.last();
				if (last.state == EasingSegmentState::Complete) {
					postprocessEasingCurve(
						last,
						(*previous).toObject());
				}
			}
            m_value = T();
        } else
            m_value = getValue(definition.value(QStringLiteral("k")));
    }

    void setValue(const T& value)
    {
        m_value = value;
    }

    const T& value() const
    {
        return m_value;
    }

    bool animated() const {
        return m_animated;
    }

    virtual bool update(int frame)
    {
        if (!m_animated)
            return false;

        int adjustedFrame = qBound(m_startFrame, frame, m_endFrame);
        if (const EasingSegment<T> *easing = getEasingSegment(adjustedFrame)) {
            qreal progress;
            if (easing->endFrame == easing->startFrame)
                progress = 1;
            else
                progress = ((adjustedFrame - easing->startFrame) * 1.0) /
                        (easing->endFrame - easing->startFrame);
            qreal easedValue = easing->easing.valueForProgress(progress);
            m_value = easing->startValue + easedValue *
                    ((easing->endValue - easing->startValue));
            return true;
        }
        return false;
    }

protected:
    void addEasing(EasingSegment<T>& easing)
    {
        if (m_easingCurves.length()) {
            EasingSegment<T> &prevEase = m_easingCurves.last();
            // The end value has to be hand picked to the
            // previous easing segment, as the json data does
            // not contain end values for segments
            prevEase.endFrame = easing.startFrame;
            if (prevEase.state == EasingSegmentState::Incomplete) {
                prevEase.endValue = easing.startValue;
                prevEase.state = EasingSegmentState::Complete;
            }
        }
        m_easingCurves.push_back(easing);
    }

    void finalizeEasingCurves()
    {
        if (m_easingCurves.length()) {
            EasingSegment<T> &last = m_easingCurves.last();
            if (last.state == EasingSegmentState::Incomplete) {
                last.endValue = last.startValue;
                last.endFrame = last.startFrame;
                this->m_endFrame = last.startFrame;
                last.state = EasingSegmentState::Final;
            }
        }
    }

    const EasingSegment<T>* getEasingSegment(int frame)
    {
        // TODO: Improve with a faster search algorithm
        const EasingSegment<T> *easing = m_currentEasing;
        if (!easing || easing->startFrame < frame ||
                easing->endFrame > frame) {
            for (int i=0; i < m_easingCurves.length(); i++) {
                if (m_easingCurves.at(i).startFrame <= frame &&
                        m_easingCurves.at(i).endFrame >= frame) {
                    m_currentEasing = &m_easingCurves.at(i);
                    break;
                }
            }
        }

        if (!m_currentEasing) {
            qCWarning(lcLottieQtBodymovinParser)
                    << "Property is animated but easing cannot be found";
        }
        return m_currentEasing;
    }

    virtual EasingSegment<T> parseKeyframe(const QJsonObject keyframe)
    {
        EasingSegment<T> easing;

        int startTime = keyframe.value(QStringLiteral("t")).toVariant().toInt();

        // AE exported Bodymovin file includes the last
        // key frame but no other properties.
        // No need to process in that case
        if (!keyframe.contains(QStringLiteral("s")) && !keyframe.contains(QStringLiteral("e"))) {
            // In this case start time is the last frame for the property
            this->m_endFrame = startTime;
            easing.startFrame = startTime;
            easing.endFrame = startTime;
            easing.state = EasingSegmentState::Final;
            if (m_easingCurves.length()) {
                const EasingSegment<T> &last = m_easingCurves.last();
                if (last.state == EasingSegmentState::Complete) {
                    easing.startValue = last.endValue;
                    easing.endValue = last.endValue;
                } else {
                    qCWarning(lcLottieQtBodymovinParser())
                            << "Last keyframe found after an incomplete one";
                }
            }
            return easing;
        }

        if (m_startFrame > startTime)
            m_startFrame = startTime;

        easing.startFrame = startTime;
        easing.startValue = getValue(keyframe.value(QStringLiteral("s")).toArray());
        if (keyframe.contains(QStringLiteral("e"))) {
            easing.endValue = getValue(keyframe.value(QStringLiteral("e")).toArray());
            easing.state = EasingSegmentState::Complete;
        }

        QJsonObject easingIn = keyframe.value(QStringLiteral("i")).toObject();
        QJsonObject easingOut = keyframe.value(QStringLiteral("o")).toObject();

        qreal eix = easingIn.value(QStringLiteral("x")).toArray().at(0).toDouble();
        qreal eiy = easingIn.value(QStringLiteral("y")).toArray().at(0).toDouble();

        qreal eox = easingOut.value(QStringLiteral("x")).toArray().at(0).toDouble();
        qreal eoy = easingOut.value(QStringLiteral("y")).toArray().at(0).toDouble();

        QPointF c1 = QPointF(eox, eoy);
        QPointF c2 = QPointF(eix, eiy);

        easing.easing.addCubicBezierSegment(c1, c2, QPointF(1.0, 1.0));

        return easing;
    }

	virtual void postprocessEasingCurve(
		EasingSegment<T> &easing,
		const QJsonObject keyframe) {
	}

    virtual T getValue(const QJsonValue &value)
    {
        if (value.isArray())
            return getValue(value.toArray());
        else {
            QVariant val = value.toVariant();
            if (val.canConvert<T>()) {
                T t = val.value<T>();
                return t;
            }
            else
                return T();
        }
    }

    virtual T getValue(const QJsonArray &value)
    {
        QVariant val = value.at(0).toVariant();
        if (val.canConvert<T>()) {
            T t = val.value<T>();
            return t;
        }
        else
            return T();
    }

protected:
    bool m_animated = false;
    QList<EasingSegment<T>> m_easingCurves;
    const EasingSegment<T> *m_currentEasing = nullptr;
    int m_startFrame = INT_MAX;
    int m_endFrame = 0;
    T m_value;
};


template <typename T>
class BODYMOVIN_EXPORT BMProperty2D : public BMProperty<T>
{
protected:
    T getValue(const QJsonArray &value) override
    {
        if (value.count() > 1)
            return T(value.at(0).toDouble(),
                     value.at(1).toDouble());
        else
            return T();
    }

    EasingSegment<T> parseKeyframe(const QJsonObject keyframe) override
    {
        QJsonArray startValues = keyframe.value(QStringLiteral("s")).toArray();
        QJsonArray endValues = keyframe.value(QStringLiteral("e")).toArray();
        int startTime = keyframe.value(QStringLiteral("t")).toVariant().toInt();

        EasingSegment<T> easingCurve;
        easingCurve.startFrame = startTime;

        // AE exported Bodymovin file includes the last
        // key frame but no other properties.
        // No need to process in that case
        if (startValues.isEmpty() && endValues.isEmpty()) {
            // In this case start time is the last frame for the property
            this->m_endFrame = startTime;
            easingCurve.startFrame = startTime;
            easingCurve.endFrame = startTime;
            easingCurve.state = EasingSegmentState::Final;
            if (this->m_easingCurves.length()) {
                const EasingSegment<T> &last = this->m_easingCurves.last();
                if (last.state == EasingSegmentState::Complete) {
                    easingCurve.startValue = last.endValue;
                    easingCurve.endValue = last.endValue;
                } else {
                    qCWarning(lcLottieQtBodymovinParser())
                            << "Last keyframe found after an incomplete one";
                }
            }
            return easingCurve;
        }

        if (this->m_startFrame > startTime)
            this->m_startFrame = startTime;

        qreal xs, ys;
        xs = startValues.at(0).toDouble();
        ys = startValues.at(1).toDouble();
        T s(xs, ys);

        QJsonObject easingIn = keyframe.value(QStringLiteral("i")).toObject();
        QJsonObject easingOut = keyframe.value(QStringLiteral("o")).toObject();

        easingCurve.startFrame = startTime;
        easingCurve.startValue = s;
        if (!endValues.isEmpty()) {
            qreal xe, ye;
            xe = endValues.at(0).toDouble();
            ye = endValues.at(1).toDouble();
            T e(xe, ye);
            easingCurve.endValue = e;
            easingCurve.state = EasingSegmentState::Complete;
        }

        if (easingIn.value(QStringLiteral("x")).isArray()) {
            QJsonArray eixArr = easingIn.value(QStringLiteral("x")).toArray();
            QJsonArray eiyArr = easingIn.value(QStringLiteral("y")).toArray();

            QJsonArray eoxArr = easingOut.value(QStringLiteral("x")).toArray();
            QJsonArray eoyArr = easingOut.value(QStringLiteral("y")).toArray();

            if (!eixArr.isEmpty() && !eiyArr.isEmpty()) {
                qreal eix = eixArr.takeAt(0).toDouble();
                qreal eiy = eiyArr.takeAt(0).toDouble();

                qreal eox = eoxArr.takeAt(0).toDouble();
                qreal eoy = eoyArr.takeAt(0).toDouble();

                QPointF c1 = QPointF(eox, eoy);
                QPointF c2 = QPointF(eix, eiy);

                easingCurve.easing.addCubicBezierSegment(c1, c2, QPointF(1.0, 1.0));
            }
        }
        else {
            qreal eix = easingIn.value(QStringLiteral("x")).toDouble();
            qreal eiy = easingIn.value(QStringLiteral("y")).toDouble();

            qreal eox = easingOut.value(QStringLiteral("x")).toDouble();
            qreal eoy = easingOut.value(QStringLiteral("y")).toDouble();

            QPointF c1 = QPointF(eox, eoy);
            QPointF c2 = QPointF(eix, eiy);

            easingCurve.easing.addCubicBezierSegment(c1, c2, QPointF(1.0, 1.0));
        }

        return easingCurve;
    }
};

template <typename T>
class BODYMOVIN_EXPORT BMProperty4D : public BMProperty<T>
{
public:
    bool update(int frame) override
    {
        if (!this->m_animated)
            return false;

		int adjustedFrame = qBound(this->m_startFrame, frame, this->m_endFrame);
		if (const EasingSegment<T> *easing = BMProperty<T>::getEasingSegment(adjustedFrame)) {
			qreal progress;
			if (easing->endFrame == easing->startFrame)
				progress = 1;
			else
				progress = ((adjustedFrame - easing->startFrame) * 1.0) /
				(easing->endFrame - easing->startFrame);
			qreal easedValue = easing->easing.valueForProgress(progress);
            // For the time being, 4D vectors are used only for colors, and
            // the value must be restricted to between [0, 1]
            easedValue = qBound(qreal(0.0), easedValue, qreal(1.0));
            T sv = easing->startValue;
            T ev = easing->endValue;
            qreal x = sv.x() + easedValue * (ev.x() - sv.x());
            qreal y = sv.y() + easedValue * (ev.y() - sv.y());
            qreal z = sv.z() + easedValue * (ev.z() - sv.z());
            qreal w = sv.w() + easedValue * (ev.w() - sv.w());
            this->m_value = T(x, y, z, w);
        }

        return true;
    }

protected:
    T getValue(const QJsonArray &value) override
    {
        if (value.count() > 3)
            return T(value.at(0).toDouble(), value.at(1).toDouble(),
                     value.at(2).toDouble(), value.at(3).toDouble());
        else
            return T();
    }
};

QT_END_NAMESPACE

#endif // BMPROPERTY_P_H
