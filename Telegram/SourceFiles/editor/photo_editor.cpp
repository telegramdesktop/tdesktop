/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor.h"

#include "editor/photo_editor_content.h"
#include "editor/photo_editor_controls.h"
#include "styles/style_editor.h"

namespace Editor {

PhotoEditor::PhotoEditor(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<QPixmap> photo)
: RpWidget(parent)
, _content(base::make_unique_q<PhotoEditorContent>(this, photo))
, _controls(base::make_unique_q<PhotoEditorControls>(this)) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto geometry = QRect(QPoint(), size);
		const auto contentRect = geometry
			- style::margins(0, 0, 0, st::photoEditorControlsHeight);
		_content->setGeometry(contentRect);
		const auto controlsRect = geometry
			- style::margins(0, contentRect.height(), 0, 0);
		_controls->setGeometry(controlsRect);
	}, lifetime());
}

} // namespace Editor
