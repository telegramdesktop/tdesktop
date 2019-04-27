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

#ifndef BMLAYER_P_H
#define BMLAYER_P_H

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

#include <QtBodymovin/private/bmbase_p.h>

QT_BEGIN_NAMESPACE

class LottieRenderer;

class BODYMOVIN_EXPORT BMLayer : public BMBase
{
public:
    enum MatteClipMode {NoClip, Alpha, InvertedAlpha, Luminence, InvertedLuminence};

    BMLayer() = default;
   explicit  BMLayer (const BMLayer &other);
    ~BMLayer() override;

    BMBase *clone() const override;

    static BMLayer *construct(QJsonObject definition);

    bool active(int frame) const override;

    void  parse(const QJsonObject &definition) override;

    void updateProperties(int frame) override;
    void render(LottieRenderer &renderer) const override;

    BMBase *findChild(const QString &childName) override;

    bool isClippedLayer() const;
    bool isMaskLayer() const;
    MatteClipMode clipMode() const;

    int layerId() const;
    BMBasicTransform *transform() const;

protected:
    void renderEffects(LottieRenderer &renderer) const;

    virtual BMLayer *resolveLinkedLayer();
    virtual BMLayer *linkedLayer() const;

    int m_layerIndex = 0;
    int m_startFrame;
    int m_endFrame;
    qreal m_startTime;
    int m_blendMode;
    bool m_3dLayer = false;
    BMBase *m_effects = nullptr;
    qreal m_stretch;
    BMBasicTransform *m_layerTransform = nullptr;

    int m_parentLayer = 0;
    int m_td = 0;
    MatteClipMode m_clipMode = NoClip;

private:
    void parseEffects(const QJsonArray &definition, BMBase *effectRoot = nullptr);

    BMLayer *m_linkedLayer = nullptr;
};

QT_END_NAMESPACE

#endif // BMLAYER_P_H
