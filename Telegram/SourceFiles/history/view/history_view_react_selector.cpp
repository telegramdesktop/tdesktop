/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_react_selector.h"

#include "history/view/history_view_react_button.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_message_reactions.h"
#include "data/data_peer_values.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "ui/widgets/popup_menu.h"
#include "history/history.h"
#include "history/history_item.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
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

PopupSelector::PopupSelector(
	not_null<QWidget*> parent,
	PossibleReactions reactions)
: RpWidget(parent) {
}

int PopupSelector::countWidth(int desiredWidth, int maxWidth) {
	return maxWidth;
}

QMargins PopupSelector::extentsForShadow() const {
	return st::defaultPopupMenu.shadow.extend;
}

int PopupSelector::extendTopForCategories() const {
	return st::emojiFooterHeight;
}

int PopupSelector::desiredHeight() const {
	return st::emojiPanMaxHeight;
}

void PopupSelector::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), QColor(0, 128, 0, 128));
}

[[nodiscard]] PossibleReactions LookupPossibleReactions(
		not_null<HistoryItem*> item) {
	if (!item->canReact()) {
		return {};
	}
	auto result = PossibleReactions();
	const auto peer = item->history()->peer;
	const auto session = &peer->session();
	const auto reactions = &session->data().reactions();
	const auto &full = reactions->list(Data::Reactions::Type::Active);
	const auto &all = item->reactions();
	const auto my = item->chosenReaction();
	auto myIsUnique = false;
	for (const auto &[id, count] : all) {
		if (count == 1 && id == my) {
			myIsUnique = true;
		}
	}
	const auto notMineCount = int(all.size()) - (myIsUnique ? 1 : 0);
	const auto limit = Data::UniqueReactionsLimit(peer);
	if (limit > 0 && notMineCount >= limit) {
		result.recent.reserve(all.size());
		for (const auto &reaction : full) {
			const auto id = reaction.id;
			if (all.contains(id)) {
				result.recent.push_back(id);
			}
		}
	} else {
		const auto filter = Data::PeerReactionsFilter(peer);
		result.recent.reserve(filter.allowed
			? filter.allowed->size()
			: full.size());
		for (const auto &reaction : full) {
			const auto id = reaction.id;
			const auto emoji = filter.allowed ? id.emoji() : QString();
			if (filter.allowed
				&& (emoji.isEmpty() || !filter.allowed->contains(emoji))) {
				continue;
			} else if (reaction.premium
				&& !session->premium()
				&& !all.contains(id)) {
				if (session->premiumPossible()) {
					result.morePremiumAvailable = true;
				}
				continue;
			} else {
				result.recent.push_back(id);
			}
		}
		result.customAllowed = peer->isUser();
	}
	const auto i = ranges::find(result.recent, reactions->favorite());
	if (i != end(result.recent) && i != begin(result.recent)) {
		std::rotate(begin(result.recent), i, i + 1);
	}
	return result;
}

bool SetupSelectorInMenuGeometry(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition,
		not_null<PopupSelector*> selector) {
	const auto extend = st::reactStripExtend;
	const auto added = extend.left() + extend.right();
	const auto desiredWidth = menu->width() + added;
	const auto maxWidth = menu->st().menu.widthMax + added;
	const auto width = selector->countWidth(desiredWidth, maxWidth);
	const auto extents = selector->extentsForShadow();
	const auto categoriesTop = selector->extendTopForCategories();
	menu->setForceWidth(width - added);
	const auto height = menu->height();
	const auto fullTop = extents.top() + categoriesTop + extend.top();
	const auto minimalHeight = extents.top()
		+ std::min(
			selector->desiredHeight(),
			categoriesTop + st::emojiPanMinHeight / 2)
		+ extents.bottom();
	const auto willBeHeightWithoutBottomPadding = fullTop
		+ height
		- menu->st().shadow.extend.top();
	const auto additionalPaddingBottom
		= (willBeHeightWithoutBottomPadding < minimalHeight
			? (minimalHeight - willBeHeightWithoutBottomPadding)
			: 0);
	menu->setAdditionalMenuPadding(QMargins(
		extents.left() + extend.left(),
		fullTop,
		extents.right() + extend.right(),
		additionalPaddingBottom
	), QMargins(
		extents.left(),
		extents.top(),
		extents.right(),
		std::min(additionalPaddingBottom, extents.bottom())
	));
	if (!menu->prepareGeometryFor(desiredPosition)) {
		return false;
	}
	const auto origin = menu->preparedOrigin();
	if (!additionalPaddingBottom
		|| origin == Ui::PanelAnimation::Origin::TopLeft
		|| origin == Ui::PanelAnimation::Origin::TopRight) {
		return true;
	}
	menu->setAdditionalMenuPadding(QMargins(
		extents.left() + extend.left(),
		fullTop + additionalPaddingBottom,
		extents.right() + extend.right(),
		0
	), QMargins(
		extents.left(),
		extents.top(),
		extents.right(),
		0
	));
	return menu->prepareGeometryFor(desiredPosition);
}

AttachSelectorResult AttachSelectorToMenu(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition,
		not_null<HistoryItem*> item) {
	auto reactions = LookupPossibleReactions(item);
	if (reactions.recent.empty() && !reactions.morePremiumAvailable) {
		return AttachSelectorResult::Skipped;
	}
	const auto selector = Ui::CreateChild<PopupSelector>(
		menu.get(),
		std::move(reactions));
	if (!SetupSelectorInMenuGeometry(menu, desiredPosition, selector)) {
		return AttachSelectorResult::Failed;
	}
	const auto extents = selector->extentsForShadow();
	const auto categoriesTop = selector->extendTopForCategories();
	selector->setGeometry(
		extents.left(),
		menu->preparedPadding().top() - st::reactStripExtend.top(),
		menu->width() - extents.left() - extents.right(),
		st::reactStripHeight);
	selector->lower();
	selector->show();
	return AttachSelectorResult::Attached;
}

} // namespace HistoryView::Reactions
