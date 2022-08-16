/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_react_selector.h"

#include "history/view/history_view_react_button.h"
#include "data/data_document.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView::Reactions {

void Selector::show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> widget,
		FullMsgId contextId,
		QRect around) {
	if (!_panel) {
		create(controller);
	} else if (_contextId == contextId
		&& (!_panel->hiding() && !_panel->isHidden())) {
		return;
	}
	_contextId = contextId;
	const auto parent = _panel->parentWidget();
	const auto global = widget->mapToGlobal(around.topLeft());
	const auto local = parent->mapFromGlobal(global);
	const auto availableTop = local.y();
	const auto availableBottom = parent->height()
		- local.y()
		- around.height();
	if (availableTop >= st::emojiPanMinHeight
		|| availableTop >= availableBottom) {
		_panel->setDropDown(false);
		_panel->moveBottomRight(
			local.y(),
			local.x() + around.width() * 3);
	} else {
		_panel->setDropDown(true);
		_panel->moveTopRight(
			local.y() + around.height(),
			local.x() + around.width() * 3);
	}
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->showAnimated();
}

rpl::producer<ChosenReaction> Selector::chosen() const {
	return _chosen.events();
}

rpl::producer<bool> Selector::shown() const {
	return _shown.events();
}

void Selector::create(
		not_null<Window::SessionController*> controller) {
	using Selector = ChatHelpers::TabbedSelector;
	_panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		controller->window().widget()->bodyWidget(),
		controller,
		object_ptr<Selector>(
			nullptr,
			controller,
			Window::GifPauseReason::Layer,
			ChatHelpers::TabbedSelector::Mode::EmojiStatus));
	_panel->shownValue() | rpl::start_to_stream(_shown, _panel->lifetime());
	_panel->hide();
	_panel->selector()->setAllowEmojiWithoutPremium(false);

	auto statusChosen = _panel->selector()->customEmojiChosen(
	) | rpl::map([=](Selector::FileChosen data) {
		return data.document->id;
	});

	rpl::merge(
		std::move(statusChosen),
		_panel->selector()->emojiChosen() | rpl::map_to(DocumentId())
	) | rpl::start_with_next([=](DocumentId id) {
		_chosen.fire(ChosenReaction{ .context = _contextId, .id = { id } });
	}, _panel->lifetime());

	_panel->selector()->showPromoForPremiumEmoji();
}

void Selector::hide(anim::type animated) {
	if (!_panel || _panel->isHidden()) {
		return;
	} else if (animated == anim::type::instant) {
		_panel->hideFast();
	} else {
		_panel->hideAnimated();
	}
}

} // namespace HistoryView::Reactions
