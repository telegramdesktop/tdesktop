/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/photo_editor_common.h"
#include "media/media_video_encode.h"

namespace Editor {

struct VideoModifications {
	PhotoModifications geometry;
	crl::time from = 0;
	crl::time till = 0;
	crl::time cover = 0;
};

struct VideoEditorData {
	EditorData editor;

	QString hint;

	QSize exactSize;

	crl::time maxDuration = 0;
	crl::time minDuration = 0;
	float64 fpsLimit = 0.;
	bool removeAudio = false;
};

[[nodiscard]] Media::Encode::VideoSource ComposeVideoSource(
	const QString &path,
	const VideoModifications &modifications,
	const VideoEditorData &data);

[[nodiscard]] QImage ExtractCoverImage(
	const QString &path,
	const VideoModifications &modifications,
	QSize dimensions,
	int side);

} // namespace Editor
