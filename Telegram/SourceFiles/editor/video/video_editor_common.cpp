/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_editor_common.h"

#include "media/media_video_frames.h"

namespace Editor {

QSize EditedFrameSize(QSize dimensions, const PhotoModifications &geometry) {
	if (dimensions.isEmpty()) {
		return {};
	}
	const auto full = QRect(QPoint(), dimensions);
	const auto crop = geometry.crop.isValid()
		? (geometry.crop & full)
		: full;
	auto result = crop.isEmpty() ? full.size() : crop.size();
	if (((geometry.angle / 90) % 2) != 0) {
		result.transpose();
	}
	return result;
}

bool VideoEdited(
		const VideoModifications &modifications,
		QSize dimensions,
		crl::time duration,
		bool hasAudio) {
	const auto &geometry = modifications.geometry;
	const auto full = QRect(QPoint(), dimensions);
	// Either the audio has to be dropped, or an empty track has to be added.
	const auto audioChanged = (modifications.gif == hasAudio);
	return (modifications.quality > 0)
		|| audioChanged
		|| geometry.angle
		|| geometry.flipped
		|| (geometry.crop.isValid() && (geometry.crop != full))
		|| (modifications.from > 0)
		|| ((modifications.till > 0) && (modifications.till < duration));
}

Media::Encode::VideoSource ComposeVideoSource(
		const QString &path,
		const VideoModifications &modifications,
		const VideoEditorData &data,
		bool coverNeeded) {
	const auto &geometry = modifications.geometry;
	return {
		.path = path,
		.targetShorterSide = (data.exactSize.isEmpty()
			? modifications.quality
			: 0),
		.exactSize = data.exactSize,
		.crop = geometry.crop,
		.rotation = geometry.angle,
		.flipped = geometry.flipped,
		.from = modifications.from,
		.till = modifications.till,
		.removeAudio = (data.removeAudio || modifications.gif),
		.silentAudio = (!data.removeAudio && !modifications.gif),
		.fpsLimit = data.fpsLimit,
		.coverPosition = (coverNeeded
			? modifications.cover
			: crl::time(-1)),
	};
}

QImage ExtractCoverImage(
		const QString &path,
		const VideoModifications &modifications,
		QSize dimensions,
		int side) {
	if (dimensions.isEmpty() || side <= 0) {
		return {};
	}
	auto mods = modifications.geometry;
	mods.paint = nullptr;
	const auto crop = mods.crop;
	const auto cropSide = crop.isValid()
		? std::min(crop.width(), crop.height())
		: std::min(dimensions.width(), dimensions.height());
	const auto ratio = (cropSide > 0)
		? std::min(side / float64(cropSide), 1.)
		: 1.;
	const auto box = QSize(
		std::max(int(base::SafeRound(dimensions.width() * ratio)), 2),
		std::max(int(base::SafeRound(dimensions.height() * ratio)), 2));
	auto frame = Media::Video::ExtractFrame(path, modifications.cover, box);
	if (frame.isNull()) {
		return {};
	}
	if (crop.isValid()) {
		const auto scale = frame.width() / float64(dimensions.width());
		mods.crop = QRect(
			int(base::SafeRound(crop.x() * scale)),
			int(base::SafeRound(crop.y() * scale)),
			std::max(int(base::SafeRound(crop.width() * scale)), 1),
			std::max(int(base::SafeRound(crop.height() * scale)), 1));
	}
	return ImageModified(std::move(frame), mods);
}

} // namespace Editor
