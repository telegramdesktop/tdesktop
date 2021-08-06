/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "ui/effects/animations.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_inner_common.h"

namespace Ui {
class IconButton;
} // namespace Ui

namespace Editor {

class EdgeButton;
class ButtonBar;
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
	[[nodiscard]] rpl::producer<QPoint> colorLinePositionValue() const;
	[[nodiscard]] rpl::producer<bool> colorLineShownValue() const;

	[[nodiscard]] bool animating() const;

	bool handleKeyPress(not_null<QKeyEvent*> e) const;

	void applyMode(const PhotoEditorMode &mode);

private:
	void showAnimated(
		PhotoEditorMode::Mode mode,
		anim::type animated = anim::type::normal);

	int bottomButtonsTop() const;

	const style::color &_bg;
	const int _buttonHeight;
	const base::unique_qptr<ButtonBar> _transformButtons;
	const base::unique_qptr<ButtonBar> _paintTopButtons;
	const base::unique_qptr<ButtonBar> _paintBottomButtons;

	const base::unique_qptr<EdgeButton> _transformCancel;
	const base::unique_qptr<Ui::IconButton> _rotateButton;
	const base::unique_qptr<Ui::IconButton> _flipButton;
	const base::unique_qptr<Ui::IconButton> _paintModeButton;
	const base::unique_qptr<EdgeButton> _transformDone;

	const base::unique_qptr<EdgeButton> _paintCancel;
	const base::unique_qptr<Ui::IconButton> _undoButton;
	const base::unique_qptr<Ui::IconButton> _redoButton;
	const base::unique_qptr<Ui::IconButton> _paintModeButtonActive;
	const base::unique_qptr<Ui::IconButton> _stickersButton;
	const base::unique_qptr<EdgeButton> _paintDone;

	bool _flipped = false;

	Ui::Animations::Simple _toggledBarAnimation;

	rpl::variable<PhotoEditorMode> _mode;
	rpl::event_stream<int> _keyPresses;

};

} // namespace Editor
