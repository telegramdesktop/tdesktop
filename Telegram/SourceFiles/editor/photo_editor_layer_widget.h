/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once
//
//#include "ui/image/image.h"
//#include "editor/photo_editor_common.h"
//#include "base/unique_qptr.h"

enum class ImageRoundRadius;

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
	not_null<Window::SessionController*> controller,
	not_null<Ui::PreparedFile*> file,
	int previewWidth,
	Fn<void()> &&doneCallback);

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
