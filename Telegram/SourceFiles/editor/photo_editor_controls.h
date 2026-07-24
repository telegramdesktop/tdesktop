/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_inner_common.h"

namespace Ui {
class AbstractButton;
class IconButton;
class FlatLabel;
class PopupMenu;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Editor {

class EdgeButton;
class ButtonBar;
struct Controllers;
struct EditorData;

class PhotoEditorControls final : public Ui::RpWidget {
public:
	PhotoEditorControls(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Controllers> controllers,
		const PhotoModifications modifications,
		const EditorData &data,
		const QSize &imageSize);

	[[nodiscard]] rpl::producer<int> rotateRequests() const;
	[[nodiscard]] rpl::producer<> flipRequests() const;
	[[nodiscard]] rpl::producer<> paintModeRequests() const;
	[[nodiscard]] rpl::producer<> textRequests() const;
	[[nodiscard]] rpl::producer<> doneRequests() const;
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<QPoint> colorLinePositionValue() const;
	[[nodiscard]] rpl::producer<bool> colorLineShownValue() const;
	[[nodiscard]] rpl::producer<float64> aspectRatioChanges() const;
	[[nodiscard]] auto cornersLevelChanges() const
		-> rpl::producer<RoundedCornersLevel>;

	[[nodiscard]] bool animating() const;

	bool handleKeyPress(not_null<QKeyEvent*> e) const;

	void applyMode(const PhotoEditorMode &mode);

private:
	void showAnimated(
		PhotoEditorMode::Mode mode,
		anim::type animated = anim::type::normal);
	void updateInputMask();

	int bottomButtonsTop() const;

	const QSize _imageSize;
	const style::color &_bg;
	const int _buttonHeight;
	const base::unique_qptr<ButtonBar> _transformButtons;
	const base::unique_qptr<ButtonBar> _paintTopButtons;
	const base::unique_qptr<ButtonBar> _paintBottomButtons;

	const base::unique_qptr<Ui::FadeWrap<Ui::FlatLabel>> _about;

	const base::unique_qptr<EdgeButton> _transformCancel;
	const base::unique_qptr<Ui::IconButton> _flipButton;
	const base::unique_qptr<Ui::IconButton> _rotateButton;
	const base::unique_qptr<Ui::IconButton> _paintModeButton;
	const base::unique_qptr<Ui::IconButton> _cropRatioButton;
	const base::unique_qptr<Ui::IconButton> _cornersButton;
	const base::unique_qptr<EdgeButton> _transformDone;

	const base::unique_qptr<EdgeButton> _paintCancel;
	const base::unique_qptr<Ui::IconButton> _undoButton;
	const base::unique_qptr<Ui::IconButton> _redoButton;
	const base::unique_qptr<Ui::IconButton> _paintModeButtonActive;
	const base::unique_qptr<Ui::IconButton> _stickersButton;
	const base::unique_qptr<Ui::AbstractButton> _textButton;
	const base::unique_qptr<EdgeButton> _paintDone;

	base::unique_qptr<Ui::PopupMenu> _ratioMenu;
	base::unique_qptr<Ui::PopupMenu> _cornersMenu;
	float64 _currentRatio = 0.;
	RoundedCornersLevel _currentCornersLevel = RoundedCornersLevel::Large;

	bool _flipped = false;

	Ui::Animations::Simple _toggledBarAnimation;

	rpl::variable<PhotoEditorMode> _mode;
	rpl::event_stream<not_null<QKeyEvent*>> _keyPresses;
	rpl::event_stream<float64> _aspectRatioChanges;
	rpl::event_stream<RoundedCornersLevel> _cornersLevelChanges;

};

} // namespace Editor
