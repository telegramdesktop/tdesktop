/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/controllers/stickers_panel_controller.h"
#include "editor/controllers/undo_controller.h"
#include "ui/layers/box_content.h"

namespace Editor {

struct Controllers final {
	Controllers(
		std::unique_ptr<StickersPanelController> stickersPanelController,
		std::unique_ptr<UndoController> undoController,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox)
	: stickersPanelController(std::move(stickersPanelController))
	, undoController(std::move(undoController))
	, showBox(std::move(showBox)) {
	}
	~Controllers() {
	};

	const std::unique_ptr<StickersPanelController> stickersPanelController;
	const std::unique_ptr<UndoController> undoController;
	const Fn<void(object_ptr<Ui::BoxContent>)> showBox;
};

} // namespace Editor
