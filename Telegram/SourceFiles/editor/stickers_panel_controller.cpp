/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/stickers_panel_controller.h"

#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_chat_helpers.h"

namespace Editor {

StickersPanelController::StickersPanelController(
	not_null<Ui::RpWidget*> panelContainer,
	not_null<Window::SessionController*> controller)
: _stickersPanel(
	base::make_unique_q<ChatHelpers::TabbedPanel>(
		panelContainer,
		controller,
		object_ptr<ChatHelpers::TabbedSelector>(
			nullptr,
			controller,
			ChatHelpers::TabbedSelector::Mode::Full))) {
	_stickersPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_stickersPanel->hide();
}

auto StickersPanelController::stickerChosen() const
-> rpl::producer<not_null<DocumentData*>> {
	return _stickersPanel->selector()->fileChosen(
	) | rpl::map([](const ChatHelpers::TabbedSelector::FileChosen &data) {
		return data.document;
	});
}

rpl::producer<bool> StickersPanelController::panelShown() const {
	return _stickersPanel->shownValue();
}

void StickersPanelController::setShowRequestChanges(
		rpl::producer<std::optional<bool>> &&showRequest) {
	std::move(
		showRequest
	) | rpl::start_with_next([=](std::optional<bool> show) {
		if (!show) {
			_stickersPanel->toggleAnimated();
			_stickersPanel->raise();
			return;
		}
		if (*show) {
			_stickersPanel->showAnimated();
		} else {
			_stickersPanel->toggleAnimated();
		}
	}, _stickersPanel->lifetime());
}

void StickersPanelController::setMoveRequestChanges(
		rpl::producer<QPoint> &&moveRequest) {
	std::move(
		moveRequest
	) | rpl::start_with_next([=](const QPoint &point) {
		_stickersPanel->moveBottomRight(
			point.y(),
			point.x() + _stickersPanel->width() / 2);
	}, _stickersPanel->lifetime());
}

} // namespace Editor
