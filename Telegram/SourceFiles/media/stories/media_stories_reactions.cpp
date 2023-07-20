/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_reactions.h"

#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"
#include "styles/style_widgets.h"

namespace Media::Stories {
namespace {

[[nodiscard]] Data::PossibleItemReactionsRef LookupPossibleReactions(
		not_null<Main::Session*> session) {
	auto result = Data::PossibleItemReactionsRef();
	const auto reactions = &session->data().reactions();
	const auto &full = reactions->list(Data::Reactions::Type::Active);
	const auto &top = reactions->list(Data::Reactions::Type::Top);
	const auto &recent = reactions->list(Data::Reactions::Type::Recent);
	const auto premiumPossible = session->premiumPossible();
	auto added = base::flat_set<Data::ReactionId>();
	result.recent.reserve(full.size());
	for (const auto &reaction : ranges::views::concat(top, recent, full)) {
		if (premiumPossible || !reaction.id.custom()) {
			if (added.emplace(reaction.id).second) {
				result.recent.push_back(&reaction);
			}
		}
	}
	result.customAllowed = premiumPossible;
	const auto i = ranges::find(
		result.recent,
		reactions->favoriteId(),
		&Data::Reaction::id);
	if (i != end(result.recent) && i != begin(result.recent)) {
		std::rotate(begin(result.recent), i, i + 1);
	}
	return result;
}

} // namespace

struct Reactions::Hiding {
	explicit Hiding(not_null<QWidget*> parent) : widget(parent) {
	}

	Ui::RpWidget widget;
	Ui::Animations::Simple animation;
	QImage frame;
};

Reactions::Reactions(not_null<Controller*> controller)
: _controller(controller) {
}

Reactions::~Reactions() = default;

void Reactions::show() {
	if (_shown) {
		return;
	}
	create();
	if (!_selector) {
		return;
	}
	const auto duration = st::defaultPanelAnimation.heightDuration
		* st::defaultPopupMenu.showDuration;
	_shown = true;
	_showing.start([=] { updateShowState(); }, 0., 1., duration);
	updateShowState();
	_parent->show();
}

void Reactions::hide() {
	if (!_selector) {
		return;
	}
	_selector->beforeDestroy();
	if (!anim::Disabled()) {
		fadeOutSelector();
	}
	_shown = false;
	_expanded = false;
	_showing.stop();
	_selector = nullptr;
	_parent = nullptr;
}

void Reactions::hideIfCollapsed() {
	if (!_expanded.current()) {
		hide();
	}
}

void Reactions::collapse() {
	if (_expanded.current()) {
		hide();
		show();
	}
}

void Reactions::create() {
	auto reactions = LookupPossibleReactions(
		&_controller->uiShow()->session());
	if (reactions.recent.empty() && !reactions.morePremiumAvailable) {
		return;
	}
	_parent = std::make_unique<Ui::RpWidget>(_controller->wrap().get());
	_parent->show();

	_parent->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			const auto event = static_cast<QMouseEvent*>(e.get());
			if (event->button() == Qt::LeftButton) {
				if (!_selector
					|| !_selector->geometry().contains(event->pos())) {
					collapse();
				}
			}
		}
	}, _parent->lifetime());

	_selector = std::make_unique<HistoryView::Reactions::Selector>(
		_parent.get(),
		st::storiesReactionsPan,
		_controller->uiShow(),
		std::move(reactions),
		_controller->cachedReactionIconFactory().createMethod(),
		[=](bool fast) { hide(); });

	_selector->chosen(
	) | rpl::start_with_next([=](
			HistoryView::Reactions::ChosenReaction reaction) {
		_chosen.fire_copy(reaction);
		hide();
	}, _selector->lifetime());

	_selector->premiumPromoChosen() | rpl::start_with_next([=] {
		hide();
		ShowPremiumPreviewBox(
			_controller->uiShow(),
			PremiumPreview::InfiniteReactions);
	}, _selector->lifetime());

	const auto desiredWidth = st::storiesReactionsWidth;
	const auto maxWidth = desiredWidth * 2;
	const auto width = _selector->countWidth(desiredWidth, maxWidth);
	const auto extents = _selector->extentsForShadow();
	const auto categoriesTop = _selector->extendTopForCategories();
	const auto full = extents.left() + width + extents.right();

	_shownValue = 0.;
	rpl::combine(
		_controller->layoutValue(),
		_shownValue.value()
	) | rpl::start_with_next([=](const Layout &layout, float64 shown) {
		const auto shift = int(base::SafeRound((full / 2.) * shown));
		_parent->setGeometry(QRect(
			layout.reactions.x() + layout.reactions.width() / 2 - shift,
			layout.reactions.y(),
			full,
			layout.reactions.height()));
		const auto innerTop = layout.reactions.height()
			- st::storiesReactionsBottomSkip
			- st::reactStripHeight;
		const auto maxAdded = innerTop - extents.top() - categoriesTop;
		const auto added = std::min(maxAdded, st::storiesReactionsAddedTop);
		_selector->setSpecialExpandTopSkip(added);
		_selector->initGeometry(innerTop);
	}, _selector->lifetime());

	_selector->willExpand(
	) | rpl::start_with_next([=] {
		_expanded = true;
	}, _selector->lifetime());

	_selector->escapes() | rpl::start_with_next([=] {
		collapse();
	}, _selector->lifetime());
}

void Reactions::fadeOutSelector() {
	const auto wrap = _controller->wrap().get();
	const auto geometry = Ui::MapFrom(
		wrap,
		_parent.get(),
		_selector->geometry());
	_hiding.push_back(std::make_unique<Hiding>(wrap));
	const auto raw = _hiding.back().get();
	raw->frame = Ui::GrabWidgetToImage(_selector.get());
	raw->widget.setGeometry(geometry);
	raw->widget.show();
	raw->widget.paintRequest(
	) | rpl::start_with_next([=] {
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
					&std::unique_ptr<Hiding>::get);
				if (i != end(_hiding)) {
					_hiding.erase(i);
				}
			}
		}, 1., 0., st::slideWrapDuration);
	});
}

void Reactions::updateShowState() {
	const auto progress = _showing.value(_shown ? 1. : 0.);
	const auto opacity = 1.;
	const auto appearing = _showing.animating();
	const auto toggling = false;
	_shownValue = progress;
	_selector->updateShowState(progress, opacity, appearing, toggling);
}

} // namespace Media::Stories
