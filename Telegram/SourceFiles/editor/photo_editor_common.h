/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/editor_link_pill.h"
#include "media/media_video_canvas.h"
#include "media/media_video_encode.h"

#include <QtCore/QJsonArray>
#include <QtCore/QStringList>

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Editor {

class Scene;

struct StoryVideoObject {
	QString path;
	QByteArray content;
	QImage thumbnail;
	crl::time duration = 0;
	bool hasAudio = false;

	QPointF position;
	float64 size = 0.;
	float64 scale = 1.;
	float64 rotation = 0.;
	crl::time from = 0;
	crl::time till = 0;
	float64 volume = 1.;
	bool flipped = false;

	[[nodiscard]] bool empty() const {
		return path.isEmpty() && content.isEmpty();
	}
};

struct AudioTrack {
	QString path;
	QByteArray content;
	QString title;
	QString performer;
	QImage cover;
	crl::time duration = 0;
	crl::time from = 0;
	// Zero means the end of the track.
	crl::time till = 0;
	float64 volume = 1.;

	[[nodiscard]] bool empty() const {
		return path.isEmpty() && content.isEmpty();
	}
	[[nodiscard]] crl::time length() const {
		const auto end = (till > from) ? till : duration;
		return std::max(end - from, crl::time(0));
	}
};

struct StoryOptions {
	std::shared_ptr<ChatHelpers::Show> show;
	TextWithTags caption;
	int period = 0;
	bool periodChoice = true;
	bool roundVideo = false;

	QImage canvasMedia;
	Media::Encode::CanvasBackground canvasBackground;
	StoryVideoObject videoObject;
	QJsonArray sceneItems;
	QStringList sceneFiles;
	AudioTrack storyAudio;
	bool storyAudioSelected = true;

	std::optional<LinkPreview> storyLink;
};

enum class RoundedCornersLevel {
	Circle,
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
	QString confirmVideo;
	QSize exactSize;
	CropType cropType = CropType::Rect;
	CropMode cropMode = CropMode::Hint;
	float64 originalRatio = 0.;
	bool keepAspectRatio = false;
	bool fixedCrop = false;
	bool forOtherUser = false;
	bool composeAnimated = false;
	bool composeSound = false;
	bool hasSaveButton = false;
	std::optional<StoryOptions> story;
};

struct PhotoModifications {
	int angle = 0;
	bool flipped = false;
	QRect crop;
	EditorData::CropType cropType = EditorData::CropType::Rect;
	EditorData::CropMode cropMode = EditorData::CropMode::Hint;
	RoundedCornersLevel cornersLevel = RoundedCornersLevel::Large;
	std::shared_ptr<Scene> paint = nullptr;
	QRect canvasMediaRect;
	int enhance = 0;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const;
	~PhotoModifications();

};

[[nodiscard]] QImage ImageModified(
	QImage image,
	const PhotoModifications &mods);

struct ComposeAnimatedOptions {
	int maxSide = 0;
	crl::time maxDuration = 0;
};
[[nodiscard]] Media::Encode::Job ComposeAnimatedJob(
	const QImage &image,
	const PhotoModifications &mods,
	ComposeAnimatedOptions options = {});

void ApplyShapeMask(QImage &image, const PhotoModifications &mods);

} // namespace Editor
