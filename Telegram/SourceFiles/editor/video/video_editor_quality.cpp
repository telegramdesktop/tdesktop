/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor_quality.h"

#include "lang/lang_keys.h"
#include "media/media_video_encode.h"

namespace Editor {
namespace {

constexpr auto kLevels = { 360, 480, 720, 1080 };

} // namespace

std::vector<VideoQualityLevel> VideoQualityLevels(QSize edited) {
	auto result = std::vector<VideoQualityLevel>();
	if (edited.isEmpty()) {
		return result;
	}
	const auto shorter = std::min(edited.width(), edited.height());
	for (const auto side : kLevels) {
		if (side >= shorter) {
			break;
		}
		const auto resolution = Media::Encode::DownscaledSize(edited, side);
		if (resolution.isEmpty()) {
			continue;
		}
		result.push_back({ .shorterSide = side, .resolution = resolution });
	}
	result.push_back({ .shorterSide = 0, .resolution = edited });
	return result;
}

QString VideoQualityLabel(const VideoQualityLevel &level) {
	return level.shorterSide
		? (QString::number(level.shorterSide) + 'p')
		: tr::lng_video_quality_original(tr::now);
}

} // namespace Editor
