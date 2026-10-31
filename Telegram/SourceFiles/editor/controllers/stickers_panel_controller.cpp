/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/controllers/stickers_panel_controller.h"

#include "boxes/premium_preview_box.h"
#include "base/algorithm.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/location_picker.h"
#include "ui/emoji_config.h"
#include "ui/effects/animation_value.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h" // Window::GifPauseReason

#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

#include <QPainter>

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
		.weatherButton = storyWidgets,
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

struct StickersPanelController::ReactionsHiding {
	explicit ReactionsHiding(not_null<QWidget*> parent) : widget(parent) {
	}

	Ui::RpWidget widget;
	Ui::Animations::Simple animation;
	QImage frame;
};

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

StickersPanelController::~StickersPanelController() {
	if (_reactionsSelector) {
		_reactionsSelector->beforeDestroy();
	}
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

rpl::producer<> StickersPanelController::weatherRequests() const {
	return _stickersPanel->selector()->weatherRequests();
}

void StickersPanelController::showReactionsSelector(QRect globalItemRect) {
	const auto itemRect = _panelContainer->mapFromGlobal(globalItemRect);
	if (_reactionsSelector) {
		placeReactionsSelector(itemRect);
		return;
	}
	auto reactions = Data::LookupPossibleReactions(&session());
	if (reactions.recent.empty()) {
		return;
	}
	_stickersPanel->hideAnimated();

	_reactionsSelector = std::make_unique<HistoryView::Reactions::Selector>(
		_panelContainer,
		st::storiesReactionsPan,
		_show,
		std::move(reactions),
		TextWithEntities(),
		[=](bool fast) { hideReactionsSelector(); },
		nullptr,
		nullptr,
		true,
		_panelContainer);

	_reactionsSelector->chosen(
	) | rpl::on_next([=](HistoryView::Reactions::ChosenReaction chosen) {
		_reactionPicked.fire_copy(chosen.id);
		hideReactionsSelector();
	}, _reactionsSelector->lifetime());

	_reactionsSelector->escapes(
	) | rpl::on_next([=] {
		hideReactionsSelector();
	}, _reactionsSelector->lifetime());

	_reactionsWidth = _reactionsSelector->countWidth(
		st::storiesReactionsWidth,
		st::storiesReactionsWidth * 2);
	_reactionsCategoriesTop
		= _reactionsSelector->extendTopForCategoriesAndAbout(
			_reactionsWidth);

	placeReactionsSelector(itemRect);

	const auto duration = st::defaultPanelAnimation.heightDuration
		* st::defaultPopupMenu.showDuration;
	_reactionsShowing.start(
		[=] { updateReactionsShowState(); },
		0.,
		1.,
		duration);
	updateReactionsShowState();
}

void StickersPanelController::placeReactionsSelector(QRect itemRect) {
	const auto selector = _reactionsSelector.get();
	const auto container = _panelContainer->rect();

	const auto margins = selector->marginsForShadow();
	const auto categoriesTop = _reactionsCategoriesTop;
	const auto full = margins.left() + _reactionsWidth + margins.right();
	const auto left = std::clamp(
		itemRect.center().x() - full / 2,
		0,
		std::max(container.width() - full, 0));

	const auto smallGap = st::storiesEditorReactionsSkip;
	const auto roomAbove = itemRect.y()
		- smallGap
		- st::reactStripHeight
		- margins.bottom()
		- margins.top()
		- categoriesTop;
	const auto roomBelow = container.height()
		- itemRect.y()
		- itemRect.height()
		- smallGap
		- margins.top()
		- st::reactStripHeight
		- margins.bottom();
	const auto below = (roomAbove < 0) && (roomBelow >= 0);
	selector->setBubbleUp(below);
	selector->setExpandDown(below);
	const auto innerTop = below
		? (itemRect.y()
			+ itemRect.height()
			+ smallGap
			+ margins.top())
		: std::max(
			itemRect.y() - smallGap - st::reactStripHeight - margins.bottom(),
			2 * margins.top() + categoriesTop);
	const auto maxAdded = innerTop - margins.top() - categoriesTop;
	const auto added = below
		? 0
		: std::clamp(maxAdded, 0, st::storiesReactionsAddedTop);
	selector->setSpecialExpandTopSkip(added);
	const auto widgetTop = below
		? (innerTop - margins.top())
		: std::max(maxAdded - added, 0);
	selector->initGeometry(innerTop - widgetTop);
	selector->setGeometry(QRect(QPoint(left, widgetTop), selector->size()));
}

void StickersPanelController::updateReactionsShowState() {
	if (!_reactionsSelector) {
		return;
	}
	_reactionsSelector->updateShowState(
		_reactionsShowing.value(1.),
		1.,
		_reactionsShowing.animating(),
		false);
}

void StickersPanelController::hideReactionsSelector() {
	if (!_reactionsSelector) {
		return;
	}
	_reactionsSelector->beforeDestroy();
	if (!anim::Disabled()) {
		fadeOutReactionsSelector();
	}
	_reactionsShowing.stop();
	_reactionsSelector = nullptr;
}

void StickersPanelController::fadeOutReactionsSelector() {
	_hiding.push_back(std::make_unique<ReactionsHiding>(_panelContainer));
	const auto raw = _hiding.back().get();
	raw->frame = Ui::GrabWidgetToImage(_reactionsSelector.get());
	raw->widget.setGeometry(_reactionsSelector->geometry());
	raw->widget.show();
	raw->widget.paintRequest(
	) | rpl::on_next([=] {
		if (const auto opacity = raw->animation.value(0.)) {
			auto p = QPainter(&raw->widget);
			p.setOpacity(opacity);
			p.drawImage(0, 0, raw->frame);
		}
	}, raw->widget.lifetime());
	Ui::PostponeCall(&raw->widget, [=] {
		raw->animation.start([=] {
			if (raw->animation.animating()) {
				raw->widget.update();
			} else {
				const auto i = ranges::find(
					_hiding,
					raw,
					&std::unique_ptr<ReactionsHiding>::get);
				if (i != end(_hiding)) {
					_hiding.erase(i);
				}
			}
		}, 1., 0., st::slideWrapDuration);
	});
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
			hideReactionsSelector();
			_stickersPanel->toggleAnimated();
			_stickersPanel->raise();
		} else if (show == ShowRequest::ShowAnimated) {
			hideReactionsSelector();
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
