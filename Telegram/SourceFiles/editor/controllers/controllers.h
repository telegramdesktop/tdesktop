/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/controllers/stickers_panel_controller.h"
#include "editor/controllers/undo_controller.h"
#include "ui/layers/show.h"

namespace Editor {

struct Controllers final {
	Controllers(
		std::unique_ptr<StickersPanelController> stickersPanelController,
		std::unique_ptr<UndoController> undoController,
		std::shared_ptr<Ui::Show> show)
	: stickersPanelController(std::move(stickersPanelController))
	, undoController(std::move(undoController))
	, show(std::move(show)) {
	}
	~Controllers() {
	};

	const std::unique_ptr<StickersPanelController> stickersPanelController;
	const std::unique_ptr<UndoController> undoController;
	const std::shared_ptr<Ui::Show> show;
};

} // namespace Editor
