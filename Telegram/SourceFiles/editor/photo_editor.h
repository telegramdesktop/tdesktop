/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"

#include "base/unique_qptr.h"

namespace Editor {

class ColorPicker;
class PhotoEditorContent;
class PhotoEditorControls;
class UndoController;

class PhotoEditor final : public Ui::RpWidget {
public:
	PhotoEditor(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<QPixmap> photo,
		PhotoModifications modifications);

	void save();
	rpl::producer<PhotoModifications> done() const;

private:

	PhotoModifications _modifications;

	const std::shared_ptr<UndoController> _undoController;

	base::unique_qptr<PhotoEditorContent> _content;
	base::unique_qptr<PhotoEditorControls> _controls;
	const std::unique_ptr<ColorPicker> _colorPicker;

	rpl::variable<PhotoEditorMode> _mode = PhotoEditorMode{
		.mode = PhotoEditorMode::Mode::Transform,
		.action = PhotoEditorMode::Action::None,
	};
	rpl::event_stream<PhotoModifications> _done;

};

} // namespace Editor
