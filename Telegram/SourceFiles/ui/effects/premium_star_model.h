/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <vector>

namespace Ui::Premium {

struct StarModel {
	std::vector<float> vertices;
	int vertexCount = 0;

	[[nodiscard]] bool isNull() const {
		return vertices.empty();
	}
};

[[nodiscard]] StarModel LoadStarModel();

} // namespace Ui::Premium
