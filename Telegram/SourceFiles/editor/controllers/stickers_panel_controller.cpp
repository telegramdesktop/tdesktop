/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/controllers/stickers_panel_controller.h"

#include "boxes/premium_preview_box.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_document.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/location_picker.h"
#include "ui/emoji_config.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "window/window_session_controller.h" // Window::GifPauseReason

#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

namespace Editor {
namespace {

// Works around MSVC 14.44 ICE on bitfields set in a member-init list.
[[nodiscard]] ChatHelpers::ComposeFeatures PrepareFeatures(
		bool withAudio,
		bool storyWidgets,
		bool locationAvailable) {
	return {
		.megagroupSet = false,
		.stickersSettings = false,
		.openStickerSets = false,
		.photoButton = true,
		.reactionButton = storyWidgets,
		.audioButton = withAudio,
		.linkButton = storyWidgets,
		.locationButton = storyWidgets && locationAvailable,
	};
}

void ShowStoryLinkPremiumToast(std::shared_ptr<ChatHelpers::Show> show) {
	show->showToast(Ui::Toast::Config{
		.text = tr::lng_story_link_premium(
			tr::now,
			lt_link,
			Ui::Text::Link(
				tr::lng_story_period_premium_link(tr::now),
				u"internal:premium"_q),
			Ui::Text::WithEntities),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton) {
			ShowPremiumPreviewToBuy(show, PremiumFeature::Stories);
			return true;
		},
	});
}

} // namespace

StickersPanelController::StickersPanelController(
	not_null<Ui::RpWidget*> panelContainer,
	std::shared_ptr<ChatHelpers::Show> show,
	bool withAudio,
	bool storyWidgets)
: _panelContainer(panelContainer)
, _show(show)
, _stickersPanel(
	base::make_unique_q<ChatHelpers::TabbedPanel>(
		panelContainer,
		ChatHelpers::TabbedPanelDescriptor{
			.ownedSelector = object_ptr<ChatHelpers::TabbedSelector>(
				nullptr,
				ChatHelpers::TabbedSelectorDescriptor{
					.show = show,
					.st = st::mediaviewEmojiPan,
					.level = Window::GifPauseReason::Layer,
					.mode = ChatHelpers::TabbedSelector::Mode::MediaEditor,
					.features = PrepareFeatures(
						withAudio,
						storyWidgets,
						Ui::LocationPicker::Available(
							Ui::ResolveMapsConfig(&session()))),
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

rpl::producer<> StickersPanelController::photoRequests() const {
	return _stickersPanel->selector()->photoRequests();
}

rpl::producer<> StickersPanelController::audioRequests() const {
	return _stickersPanel->selector()->audioRequests();
}

Main::Session &StickersPanelController::session() const {
	return _show->session();
}

rpl::producer<> StickersPanelController::linkRequests() const {
	return _stickersPanel->selector()->linkRequests(
	) | rpl::filter([=] {
		if (session().premium()) {
			return true;
		}
		ShowStoryLinkPremiumToast(_show);
		return false;
	});
}

auto StickersPanelController::reactionRequests() const
-> rpl::producer<Data::ReactionId> {
	return _stickersPanel->selector()->reactionRequests();
}

rpl::producer<> StickersPanelController::locationRequests() const {
	return _stickersPanel->selector()->locationRequests();
}

void StickersPanelController::showReactionsPicker(QPoint middleBottom) {
	if (!_reactionsPanel) {
		_reactionsPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
			_panelContainer,
			ChatHelpers::TabbedPanelDescriptor{
				.ownedSelector = object_ptr<ChatHelpers::TabbedSelector>(
					nullptr,
					ChatHelpers::TabbedSelectorDescriptor{
						.show = _show,
						.st = st::mediaviewEmojiPan,
						.level = Window::GifPauseReason::Layer,
						.mode = ChatHelpers::TabbedSelectorMode::FullReactions,
					}),
			});
		_reactionsPanel->setDesiredHeightValues(
			1.,
			st::emojiPanMinHeight / 2,
			st::emojiPanMinHeight);
		const auto selector = _reactionsPanel->selector();
		const auto picked = [=](Data::ReactionId id) {
			_reactionsPanel->hideAnimated();
			_reactionPicked.fire_copy(id);
		};
		selector->emojiChosen(
		) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
			picked(Data::ReactionId{ data.emoji->text() });
		}, _reactionsPanel->lifetime());
		selector->customEmojiChosen(
		) | rpl::on_next([=](ChatHelpers::FileChosen data) {
			picked(Data::ReactionId{ data.document->id });
		}, _reactionsPanel->lifetime());
	}
	_stickersPanel->hideAnimated();
	_reactionsPanel->moveBottomRight(
		middleBottom.y(),
		middleBottom.x() + _reactionsPanel->width() / 2);
	_reactionsPanel->showAnimated();
}

auto StickersPanelController::reactionPicked() const
-> rpl::producer<Data::ReactionId> {
	return _reactionPicked.events();
}

rpl::producer<bool> StickersPanelController::panelShown() const {
	return _stickersPanel->shownValue();
}

void StickersPanelController::setShowRequestChanges(
		rpl::producer<ShowRequest> &&showRequest) {
	std::move(
		showRequest
	) | rpl::on_next([=](ShowRequest show) {
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
	) | rpl::on_next([=](const QPoint &point) {
		_stickersPanel->moveBottomRight(
			point.y(),
			point.x() + _stickersPanel->width() / 2);
	}, _stickersPanel->lifetime());
}

} // namespace Editor
