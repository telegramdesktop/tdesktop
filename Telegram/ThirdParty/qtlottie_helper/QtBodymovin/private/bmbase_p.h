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

#ifndef BMBASE_P_H
#define BMBASE_P_H

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
#include <QList>

#include <QtBodymovin/bmglobal.h>
#include <QtBodymovin/private/bmconstants_p.h>

#include <QtBodymovin/private/lottierenderer_p.h>

#include <functional>

QT_BEGIN_NAMESPACE

class BMAsset;
class BMScene;

class BODYMOVIN_EXPORT BMBase
{
public:
    BMBase(BMBase *parent);
    BMBase(BMBase *parent, const BMBase &other);
	BMBase(const BMBase &other) = delete;
    virtual ~BMBase();

    virtual BMBase *clone(BMBase *parent) const;

    QString name() const;

    int type() const;
    void setType(int type);
    virtual void parse(const QJsonObject &definition);

    virtual bool active(int frame) const;
    bool hidden() const;

    inline BMBase *parent() const { return m_parent; }

    const QList<BMBase *> &children() const { return m_children; }
    void prependChild(BMBase *child);
    void appendChild(BMBase *child);

    virtual BMBase *findChild(const QString &childName);

    virtual void updateProperties(int frame);
    virtual void render(LottieRenderer &renderer, int frame) const;

    virtual void resolveAssets(const std::function<BMAsset*(BMBase*, QString)> &resolver);

protected:
    virtual BMScene *resolveTopRoot() const;
    BMScene *topRoot() const;

protected:
    int m_type = 0;
    bool m_hidden = false;
    QString m_name;
    QString m_matchName;
    bool m_autoOrient = false;

    friend class BMRasterRenderer;
    friend class BMRenderer;

private:
    BMBase * const m_parent = nullptr;
    QList<BMBase *> m_children;

    // Handle to the topmost element on which this element resides
    // Will be resolved when traversing effects
    mutable BMScene *m_topRoot = nullptr;
};

QT_END_NAMESPACE

#endif // BMBASE_P_H
