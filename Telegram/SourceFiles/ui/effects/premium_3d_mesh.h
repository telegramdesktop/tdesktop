/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <vector>

class QString;
class QShader;
class QImage;

namespace Ui::Premium {

struct Object3dMesh {
	std::vector<float> vertices;
	int vertexCount = 0;
};

[[nodiscard]] Object3dMesh LoadObject3dMesh(
	const QString &path,
	float scale,
	bool withUv);

struct Object3dModel {
	std::vector<Object3dMesh> shells;

	[[nodiscard]] bool isNull() const {
		if (shells.empty()) {
			return true;
		}
		for (const auto &shell : shells) {
			if (shell.vertices.empty()) {
				return true;
			}
		}
		return false;
	}
};

[[nodiscard]] Object3dModel LoadObject3dModel(
	const std::vector<QString> &paths,
	float scale,
	bool withUv);

[[nodiscard]] QShader LoadObject3dShader(const QString &name);
[[nodiscard]] QImage LoadObject3dFlecksTexture();

} // namespace Ui::Premium
