/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class IconButton;
} // namespace Ui

namespace Editor {

class HorizontalContainer;

class PhotoEditorControls final : public Ui::RpWidget {
public:
	PhotoEditorControls(
		not_null<Ui::RpWidget*> parent,
		bool doneControls = true);

private:

	const base::unique_qptr<HorizontalContainer> _buttonsContainer;
	const base::unique_qptr<Ui::IconButton> _rotateButton;
	const base::unique_qptr<Ui::IconButton> _flipButton;

};

} // namespace Editor
