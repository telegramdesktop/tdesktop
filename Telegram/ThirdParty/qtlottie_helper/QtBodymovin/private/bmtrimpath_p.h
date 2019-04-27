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

#ifndef BMTRIMPATH_P_H
#define BMTRIMPATH_P_H

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

#include <QPainterPath>
#include <QJsonObject>

#include <QtBodymovin/private/bmproperty_p.h>
#include <QtBodymovin/private/bmgroup_p.h>

QT_BEGIN_NAMESPACE

class BODYMOVIN_EXPORT BMTrimPath : public BMShape
{
public:
    BMTrimPath();
    BMTrimPath(const QJsonObject &definition, BMBase *parent = nullptr);
    explicit BMTrimPath(const BMTrimPath &other);

    void inherit(const BMTrimPath &other);

    BMBase *clone() const override;

    void construct(const QJsonObject &definition);

    void updateProperties(int frame) override;
    void render(LottieRenderer &renderer) const override;

    bool acceptsTrim() const override;
    void applyTrim(const BMTrimPath  &trimmer) override;

    qreal start() const;
    qreal end() const;
    qreal offset() const;
    bool simultaneous() const;

    QPainterPath trim(const QPainterPath &path) const;

protected:
    BMProperty<qreal> m_start;
    BMProperty<qreal> m_end;
    BMProperty<qreal> m_offset;
    bool m_simultaneous = false;
};

QT_END_NAMESPACE

#endif // BMTRIMPATH_P_H
