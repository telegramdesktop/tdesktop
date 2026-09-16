/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/photo_editor_inner_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

struct KeysLegendContext {
	PhotoEditorMode::Mode mode = PhotoEditorMode::Mode::Transform;
	bool fixedCrop = false;
	bool timeline = false;
};

class KeysLegendButton final
	: public Ui::IconButton
	, public Ui::AbstractTooltipShower {
public:
	KeysLegendButton(
		not_null<QWidget*> parent,
		Fn<KeysLegendContext()> context);

	void toggle();

	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

private:
	void showLegend();

	const Fn<KeysLegendContext()> _context;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Editor
