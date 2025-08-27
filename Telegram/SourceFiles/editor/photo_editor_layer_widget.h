/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

enum class ImageRoundRadius;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
struct PreparedFile;
} // namespace Ui

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Editor {

constexpr auto kProfilePhotoSize = int(640);

struct EditorData;

void OpenWithPreparedFile(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::PreparedFile*> file,
	int previewWidth,
	Fn<void(bool ok)> &&doneCallback,
	QSize exactSize = {});

void PrepareProfilePhoto(
	not_null<QWidget*> parent,
	not_null<Window::Controller*> controller,
	EditorData data,
	Fn<void(QImage &&image)> &&doneCallback,
	QImage &&image);

void PrepareProfilePhotoFromFile(
	not_null<QWidget*> parent,
	not_null<Window::Controller*> controller,
	EditorData data,
	Fn<void(QImage &&image)> &&doneCallback);

} // namespace Editor
