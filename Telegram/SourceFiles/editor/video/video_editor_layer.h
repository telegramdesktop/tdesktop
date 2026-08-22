/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/video/video_editor_common.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
struct PreparedList;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Editor {

inline constexpr auto kProfileVideoSide = 800;
inline constexpr auto kProfileVideoMaxDuration = crl::time(9600);
inline constexpr auto kProfileVideoMinDuration = crl::time(1000);
inline constexpr auto kProfileVideoFps = 30.;

struct ProfileMedia {
	QImage image;
	std::shared_ptr<Media::Encode::VideoSource> video;
};

[[nodiscard]] VideoEditorData ProfileVideoEditorData(EditorData data);

void PrepareProfileVideo(
	not_null<QWidget*> parent,
	not_null<Window::Controller*> controller,
	EditorData data,
	const QString &path,
	Fn<void(ProfileMedia&&)> &&doneCallback);

void PrepareProfileMediaFromFile(
	not_null<QWidget*> parent,
	not_null<Window::Controller*> controller,
	EditorData data,
	Fn<void(ProfileMedia&&)> &&doneCallback);

void OpenWithPreparedVideoFile(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::PreparedList*> list,
	int index,
	int previewWidth,
	Fn<void(bool ok)> &&doneCallback,
	int sideLimit = 0);

} // namespace Editor
