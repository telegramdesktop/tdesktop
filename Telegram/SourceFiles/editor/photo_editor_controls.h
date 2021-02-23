/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"

namespace Ui {
class IconButton;
} // namespace Ui

namespace Editor {

class EdgeButton;
class HorizontalContainer;
struct Controllers;

class PhotoEditorControls final : public Ui::RpWidget {
public:
	PhotoEditorControls(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Controllers> controllers,
		const PhotoModifications modifications,
		bool doneControls = true);

	[[nodiscard]] rpl::producer<int> rotateRequests() const;
	[[nodiscard]] rpl::producer<> flipRequests() const;
	[[nodiscard]] rpl::producer<> paintModeRequests() const;
	[[nodiscard]] rpl::producer<> doneRequests() const;
	[[nodiscard]] rpl::producer<> cancelRequests() const;

	void applyMode(const PhotoEditorMode &mode);

private:

	const style::color &_bg;
	const base::unique_qptr<HorizontalContainer> _transformButtons;
	const base::unique_qptr<HorizontalContainer> _paintButtons;

	const base::unique_qptr<Ui::IconButton> _rotateButton;
	const base::unique_qptr<Ui::IconButton> _flipButton;
	const base::unique_qptr<Ui::IconButton> _paintModeButton;

	const base::unique_qptr<Ui::IconButton> _undoButton;
	const base::unique_qptr<Ui::IconButton> _redoButton;
	const base::unique_qptr<Ui::IconButton> _paintModeButtonActive;

	const base::unique_qptr<EdgeButton> _cancel;
	const base::unique_qptr<EdgeButton> _done;

	bool _flipped;

	rpl::variable<PhotoEditorMode> _mode;

};

} // namespace Editor
