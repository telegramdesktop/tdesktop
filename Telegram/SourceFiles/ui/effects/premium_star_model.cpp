/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_star_model.h"

#include "ui/effects/premium_3d_mesh.h"

#include <QtCore/QString>

namespace Ui::Premium {

StarModel LoadStarModel() {
	auto mesh = LoadObject3dMesh(
		u":/gui/art/premium/star.binobj"_q,
		1.f,
		true);
	auto result = StarModel();
	result.vertices = std::move(mesh.vertices);
	result.vertexCount = mesh.vertexCount;
	return result;
}

} // namespace Ui::Premium
