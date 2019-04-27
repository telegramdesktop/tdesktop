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

#ifndef BMGFILL_P_H
#define BMGFILL_P_H

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

#include <QVector4D>
#include <QGradient>

#include <QtBodymovin/private/bmgroup_p.h>
#include <QtBodymovin/private/bmproperty_p.h>
#include <QtBodymovin/private/bmproperty_p.h>
#include <QtBodymovin/private/bmspatialproperty_p.h>

QT_BEGIN_NAMESPACE

class BODYMOVIN_EXPORT BMGFill : public BMShape
{
public:
    BMGFill() = default;
    explicit BMGFill(const BMGFill &other);
    BMGFill(const QJsonObject &definition, BMBase *parent = nullptr);
    ~BMGFill() override;

    BMBase *clone() const override;

    void updateProperties(int frame) override;
    void render(LottieRenderer &renderer) const override;

    QGradient *value() const;
    QGradient::Type gradientType() const;
    QPointF startPoint() const;
    QPointF endPoint() const;
    qreal highlightLength() const;
    qreal highlightAngle() const;
    qreal opacity() const;

private:
    void setGradient();

protected:
    BMProperty<qreal> m_opacity;
    BMSpatialProperty m_startPoint;
    BMSpatialProperty m_endPoint;
    BMProperty<qreal> m_highlightLength;
    BMProperty<qreal> m_highlightAngle;
    QList<BMProperty4D<QVector4D>> m_colors;
    QGradient *m_gradient = nullptr;

};

QT_END_NAMESPACE

#endif // BMGFILL_P_H
