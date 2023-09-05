/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Editor {

class Scene;

struct PhotoModifications {
	int angle = 0;
	bool flipped = false;
	QRect crop;
	std::shared_ptr<Scene> paint = nullptr;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const;
	~PhotoModifications();

};

struct EditorData {
	enum class CropType {
		Rect,
		Ellipse,
		RoundedRect,
	};

	TextWithEntities about;
	QString confirm;
	CropType cropType = CropType::Rect;
	bool keepAspectRatio = false;
};

[[nodiscard]] QImage ImageModified(
	QImage image,
	const PhotoModifications &mods);

} // namespace Editor
