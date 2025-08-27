/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/controllers/stickers_panel_controller.h"

#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "window/window_session_controller.h" // Window::GifPauseReason

#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

namespace Editor {

StickersPanelController::StickersPanelController(
	not_null<Ui::RpWidget*> panelContainer,
	std::shared_ptr<ChatHelpers::Show> show)
: _stickersPanel(
	base::make_unique_q<ChatHelpers::TabbedPanel>(
		panelContainer,
		ChatHelpers::TabbedPanelDescriptor{
			.ownedSelector = object_ptr<ChatHelpers::TabbedSelector>(
				nullptr,
				ChatHelpers::TabbedSelectorDescriptor{
					.show = show,
					.st = st::storiesComposeControls.tabbed,
					.level = Window::GifPauseReason::Layer,
					.mode = ChatHelpers::TabbedSelector::Mode::MediaEditor,
					.features = {
						.megagroupSet = false,
						.stickersSettings = false,
						.openStickerSets = false,
					},
				}),
		})) {
	_stickersPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_stickersPanel->hide();
}

auto StickersPanelController::stickerChosen() const
-> rpl::producer<not_null<DocumentData*>> {
	return _stickersPanel->selector()->fileChosen(
	) | rpl::map([](const ChatHelpers::FileChosen &data) {
		return data.document;
	});
}

rpl::producer<bool> StickersPanelController::panelShown() const {
	return _stickersPanel->shownValue();
}

void StickersPanelController::setShowRequestChanges(
		rpl::producer<ShowRequest> &&showRequest) {
	std::move(
		showRequest
	) | rpl::start_with_next([=](ShowRequest show) {
		if (show == ShowRequest::ToggleAnimated) {
			_stickersPanel->toggleAnimated();
			_stickersPanel->raise();
		} else if (show == ShowRequest::ShowAnimated) {
			_stickersPanel->showAnimated();
			_stickersPanel->raise();
		} else if (show == ShowRequest::HideAnimated) {
			_stickersPanel->hideAnimated();
		} else if (show == ShowRequest::HideFast) {
			_stickersPanel->hideFast();
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
