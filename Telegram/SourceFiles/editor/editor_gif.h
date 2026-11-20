/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Storage {
struct PhotoEditorMedia;
} // namespace Storage

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

void LoadGif(
	not_null<Ui::RpWidget*> context,
	not_null<DocumentData*> document,
	Fn<void(Storage::PhotoEditorMedia&&)> done);

} // namespace Editor
