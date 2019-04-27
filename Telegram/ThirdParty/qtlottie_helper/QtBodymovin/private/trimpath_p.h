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

#ifndef TRIMPATH_P_H
#define TRIMPATH_P_H

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

QT_BEGIN_NAMESPACE

class TrimPath {
public:
    TrimPath() = default;
    TrimPath(const QPainterPath &path)
        : mPath(path) {}
    TrimPath(const TrimPath &other)
        : mPath(other.mPath), mLens(other.mLens) {}
    ~TrimPath() {}

    void setPath(const QPainterPath &path) {
        mPath = path;
        mLens.clear();
    }

    QPainterPath path() const {
        return mPath;
    }

    QPainterPath trimmed(qreal f1, qreal f2, qreal offset = 0.0) const;

private:
    bool lensIsDirty() const {
        return mLens.size() != mPath.elementCount();
    }
    void updateLens() const;
    int elementAtLength(qreal len) const;
    QPointF endPointOfElement(int elemIdx) const;
    void appendTrimmedElement(QPainterPath *to, int elemIdx, bool trimStart, qreal startLen, bool trimEnd, qreal endLen) const;
    void appendStartOfElement(QPainterPath *to, int elemIdx, qreal len) const {
        appendTrimmedElement(to, elemIdx, false, 0.0, true, len);
    }
    void appendEndOfElement(QPainterPath *to, int elemIdx, qreal len) const {
        appendTrimmedElement(to, elemIdx, true, len, false, 1.0);
    }

    void appendElementRange(QPainterPath *to, int first, int last) const;

    QPainterPath mPath;
    mutable QVector<qreal> mLens;
};

QT_END_NAMESPACE

Q_DECLARE_METATYPE(TrimPath);

#endif // TRIMPATH_P_H
