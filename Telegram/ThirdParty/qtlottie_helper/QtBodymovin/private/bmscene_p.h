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

#ifndef BMSCENE_P_H
#define BMSCENE_P_H

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

#include <vector>
#include <memory>

#include <QtBodymovin/private/bmbase_p.h>

QT_BEGIN_NAMESPACE

class BMAsset;

class BODYMOVIN_EXPORT BMScene : public BMBase
{
public:
    BMScene();
    BMScene(const BMScene &other) = delete;
    BMScene &operator=(const BMScene &other) = delete;
    explicit BMScene(const QJsonObject &definition);
    virtual ~BMScene();

    BMBase *clone() const override;

	void updateProperties(int frame) override;
	void render(LottieRenderer &renderer, int frame) const override;

	int startFrame() const;
	int endFrame() const;
	int frameRate() const;
	int width() const;
	int height() const;

protected:
	BMScene *resolveTopRoot() const override;

private:
	void parse(const QJsonObject &definition);
	void resolveAllAssets();

	std::vector<std::unique_ptr<BMAsset>> _assets;
	QHash<QString, int> _assetIndexById;

	std::unique_ptr<BMBase> _blueprint;
	std::unique_ptr<BMBase> _current;

	int _startFrame = 0;
	int _endFrame = 0;
	int _frameRate = 30;
	int _width = 0;
	int _height = 0;
	QHash<QString, int> _markers;

	bool _unsupported = false;

};

QT_END_NAMESPACE

#endif // BMSCENE_P_H
