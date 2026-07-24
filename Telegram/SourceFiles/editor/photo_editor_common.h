/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Editor {

class Scene;

enum class RoundedCornersLevel {
	Large,
	Medium,
	Small,
	None,
};

[[nodiscard]] float64 RoundedCornersMultiplier(RoundedCornersLevel level);

struct EditorData {
	enum class CropType {
		Rect,
		Ellipse,
		RoundedRect,
	};

	enum class CropMode {
		Hint,
		Mask,
	};

	TextWithEntities about;
	QString confirm;
	QSize exactSize;
	CropType cropType = CropType::Rect;
	CropMode cropMode = CropMode::Hint;
	bool keepAspectRatio = false;
	bool fixedCrop = false;
};

struct PhotoModifications {
	int angle = 0;
	bool flipped = false;
	QRect crop;
	EditorData::CropType cropType = EditorData::CropType::Rect;
	EditorData::CropMode cropMode = EditorData::CropMode::Hint;
	RoundedCornersLevel cornersLevel = RoundedCornersLevel::Large;
	std::shared_ptr<Scene> paint = nullptr;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const;
	~PhotoModifications();

};

[[nodiscard]] QImage ImageModified(
	QImage image,
	const PhotoModifications &mods);

void ApplyShapeMask(QImage &image, const PhotoModifications &mods);

} // namespace Editor
