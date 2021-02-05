/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor.h"

#include "editor/photo_editor_content.h"
#include "editor/photo_editor_controls.h"

namespace Editor {

PhotoEditor::PhotoEditor(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<QPixmap> photo)
: RpWidget(parent) {
}

} // namespace Editor
