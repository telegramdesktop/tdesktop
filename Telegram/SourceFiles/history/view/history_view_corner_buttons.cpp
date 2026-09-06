/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_corner_buttons.h"

#include "ui/chat/chat_style.h"
#include "ui/controls/jump_down_button.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "base/event_filter.h"
#include "base/qt/qt_key_modifiers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "apiwrap.h"
#include "api/api_unread_things.h"
#include "data/data_document.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "lang/lang_keys.h"
#include "ui/toast/toast.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"

namespace HistoryView {
namespace {

constexpr auto kStashDelay = crl::time(150);
constexpr auto kStashArrowDuration = crl::time(160 / 1.5);
constexpr auto kStashBounceDuration = crl::time(260 / 1.5);
constexpr auto kStashDuration = kStashDelay
	+ kStashArrowDuration
	+ kStashBounceDuration;

[[nodiscard]] float64 ArrowShift(float64 value) {
	const auto from = kStashDelay / float64(kStashDuration);
	const auto till = (kStashDelay + kStashArrowDuration)
		/ float64(kStashDuration);
	return (value <= from)
		? -1.
		: (value >= till)
		? 0.
		: ((value - till) / (till - from));
}

[[nodiscard]] float64 BubbleSwing(float64 value) {
	const auto from = (kStashDelay + kStashArrowDuration)
		/ float64(kStashDuration);
	if (value <= from) {
		return 0.;
	}
	const auto bounce = (value - from) / (1. - from);
	return std::sin(2 * M_PI * bounce) * (1. - bounce);
}

} // namespace

class StashButton final : public Ui::JumpDownButton {
public:
	StashButton(
		QWidget *parent,
		const style::TwoIconButton &st,
		const style::icon &arrow,
		const style::icon &arrowOver);

	void setArrowShown(bool shown, float64 buttonVisible);
	void finishAnimating();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::TwoIconButton &_st;
	const style::icon &_arrow;
	const style::icon &_arrowOver;
	Ui::Animations::Simple _animation;
	std::optional<float64> _frozen;
	bool _arrowShown = false;

};

StashButton::StashButton(
	QWidget *parent,
	const style::TwoIconButton &st,
	const style::icon &arrow,
	const style::icon &arrowOver)
: JumpDownButton(parent, st)
, _st(st)
, _arrow(arrow)
, _arrowOver(arrowOver) {
}

void StashButton::setArrowShown(bool shown, float64 buttonVisible) {
	if (_arrowShown == shown) {
		return;
	}
	_arrowShown = shown;
	if (!shown) {
		_frozen = _animation.value(1.);
		_animation.stop();
		update();
		return;
	}
	const auto from = (buttonVisible > 0.) ? _frozen.value_or(1.) : 0.;
	_frozen = std::nullopt;
	// Simple::start() resumes from the current value, not from |from|.
	_animation = Ui::Animations::Simple();
	if (from < 1.) {
		_animation.start(
			[=] { update(); },
			from,
			1.,
			kStashDuration * (1. - from));
	}
	update();
}

void StashButton::finishAnimating() {
	_frozen = std::nullopt;
	_animation.stop();
	update();
}

void StashButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto active = isOver() || isDown();
	(active ? _st.iconBelowOver : _st.iconBelow).paint(
		p,
		_st.iconPosition,
		width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y());

	const auto value = _frozen.value_or(_animation.value(1.));
	const auto dip = QPoint(0, int(base::SafeRound(
		BubbleSwing(value) * st::historyStashBubbleTravel)));
	(active ? _st.iconAboveOver : _st.iconAbove).paint(
		p,
		_st.iconPosition + dip,
		width());

	const auto shift = ArrowShift(value);
	if (shift <= -1.) {
		return;
	}
	p.setClipRect(QRect(
		st::historyStashArrowClipPosition + dip,
		st::historyStashArrowClipSize));
	const auto skip = int(base::SafeRound(
		shift * st::historyStashArrowTravel));
	(active ? _arrowOver : _arrow).paint(
		p,
		_st.iconPosition + dip + QPoint(0, skip),
		width());
}

[[nodiscard]] object_ptr<StashButton> MakeStashButton(
		not_null<QWidget*> parent,
		not_null<const Ui::ChatStyle*> st,
		rpl::lifetime &lifetime) {
	return object_ptr<StashButton>(
		parent,
		st->value(lifetime, st::historyStash),
		st->value(lifetime, st::historyStashArrow),
		st->value(lifetime, st::historyStashArrowOver));
}

CornerButtons::CornerButtons(
	not_null<Ui::ScrollArea*> parent,
	not_null<const Ui::ChatStyle*> st,
	not_null<CornerButtonsDelegate*> delegate)
: CornerButtons(
	parent,
	[=](QEvent *e) { return parent->viewportEvent(e); },
	st,
	delegate) {
}

CornerButtons::CornerButtons(
	not_null<Ui::ElasticScroll*> parent,
	not_null<const Ui::ChatStyle*> st,
	not_null<CornerButtonsDelegate*> delegate)
: CornerButtons(
	parent,
	[=](QEvent *e) { return parent->viewportEvent(e); },
	st,
	delegate) {
}

CornerButtons::CornerButtons(
	not_null<QWidget*> parent,
	Fn<bool(QEvent*)> scrollViewportEvent,
	not_null<const Ui::ChatStyle*> st,
	not_null<CornerButtonsDelegate*> delegate)
: _parent(parent)
, _scrollViewportEvent(std::move(scrollViewportEvent))
, _delegate(delegate)
, _column(parent)
, _down(
	&_column,
	st->value(_stLifetime, st::historyToDown))
, _mentions(
	&_column,
	st->value(_stLifetime, st::historyUnreadMentions))
, _reactions(
		&_column,
		st->value(_stLifetime, st::historyUnreadReactions))
, _pollVotes(
		&_column,
		st->value(_stLifetime, st::historyUnreadPollVotes))
, _stash(MakeStashButton(&_column, st, _stLifetime))
, _stashButton(static_cast<StashButton*>(_stash.widget.data())) {
	// The buttons keep the positions they had as direct children, because the
	// column has the parent's height and shares its edge. Only they take mouse
	// input in it - the empty part of the strip is masked out in
	// updatePositions, so that a click there reaches the list under it. Until
	// the first button is shown there is nothing to mask, so the column stays
	// out of the hit test entirely.
	_column.setAttribute(Qt::WA_TransparentForMouseEvents);
	_column.show();
	_column.setVisualTabOrder(true);
	_column.setVisualTabOrderOverlay(true);
	if (const auto scroll = qobject_cast<Ui::RpWidget*>(_parent.get())) {
		// Otherwise the column, created before the list, would come first.
		scroll->setVisualTabOrder(true);
	}

	_down.widget->addClickHandler([=] { downClick(); });
	_mentions.widget->addClickHandler([=] { mentionsClick(); });
	_reactions.widget->addClickHandler([=] { reactionsClick(); });
	_pollVotes.widget->addClickHandler([=] { pollVotesClick(); });
	_stash.widget->addClickHandler([=] { _stashClicks.fire({}); });
	base::install_event_filter(_stash.widget.data(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			showStashMenu();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	_down.widget->setAccessibleName(tr::lng_jump_to_bottom(tr::now));
	_mentions.widget->setAccessibleName(tr::lng_jump_to_mention(tr::now));
	_reactions.widget->setAccessibleName(tr::lng_jump_to_reaction(tr::now));
	_pollVotes.widget->setAccessibleName(
		tr::lng_jump_to_poll_votes(tr::now));
	_stash.widget->setAccessibleName(tr::lng_stash_restore(tr::now));

	const auto filterScroll = [&](CornerButton &button) {
		button.widget->installEventFilter(this);
	};
	filterScroll(_down);
	filterScroll(_mentions);
	filterScroll(_reactions);
	filterScroll(_pollVotes);
	filterScroll(_stash);

	SendMenu::SetupUnreadMentionsMenu(_mentions.widget.data(), [=] {
		return _delegate->cornerButtonsThread();
	});
	SendMenu::SetupUnreadReactionsMenu(_reactions.widget.data(), [=] {
		return _delegate->cornerButtonsThread();
	});
	SendMenu::SetupUnreadPollVotesMenu(_pollVotes.widget.data(), [=] {
		return _delegate->cornerButtonsThread();
	});
}

void CornerButtons::updateAccessibleDescription(CornerButton &button) {
	const auto count = button.widget->unreadCount();
	button.widget->setAccessibleDescription(count
		? tr::lng_jump_unread_count(tr::now, lt_count, count)
		: QString());
	button.widget->accessibilityDescriptionChanged();
}

bool CornerButtons::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Wheel
		&& (o == _down.widget
			|| o == _mentions.widget
			|| o == _reactions.widget
			|| o == _pollVotes.widget
			|| o == _stash.widget)) {
		return _scrollViewportEvent(e);
	}
	return QObject::eventFilter(o, e);
}

void CornerButtons::downClick() {
	if (base::IsCtrlPressed() || !_replyReturn) {
		_delegate->cornerButtonsShowAtPosition(Data::UnreadMessagePosition);
	} else {
		_delegate->cornerButtonsShowAtPosition(_replyReturn->position());
	}
}

void CornerButtons::mentionsClick() {
	const auto history = lookupHistory();
	if (!history) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	const auto msgId = thread->unreadMentions().minLoaded();
	const auto already = (_delegate->cornerButtonsCurrentId().msg == msgId);

	// Mark mention voice/video message as read.
	// See https://github.com/telegramdesktop/tdesktop/issues/5623
	if (msgId && already) {
		if (const auto item = thread->owner().message(history->peer, msgId)) {
			if (const auto media = item->media()) {
				if (const auto document = media->document()) {
					if (!media->webpage()
						&& (document->isVoiceMessage()
							|| document->isVideoMessage())) {
						document->owner().markMediaRead(document);
					}
				}
			}
		}
	}
	showAt(msgId);
}

void CornerButtons::reactionsClick() {
	const auto history = lookupHistory();
	if (!history) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	showAt(thread->unreadReactions().minLoaded());
}

void CornerButtons::pollVotesClick() {
	const auto history = lookupHistory();
	if (!history) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	showAt(thread->unreadPollVotes().minLoaded());
}

void CornerButtons::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = nullptr;
}

QVector<FullMsgId> CornerButtons::replyReturns() const {
	return _replyReturns;
}

void CornerButtons::setReplyReturns(QVector<FullMsgId> replyReturns) {
	_replyReturns = std::move(replyReturns);
	computeCurrentReplyReturn();
	if (!_replyReturn) {
		calculateNextReplyReturn();
	}
}

void CornerButtons::computeCurrentReplyReturn() {
	const auto thread = _delegate->cornerButtonsThread();
	_replyReturn = (!thread || _replyReturns.empty())
		? nullptr
		: thread->owner().message(_replyReturns.back());
}

void CornerButtons::skipReplyReturn(FullMsgId id) {
	while (_replyReturn) {
		if (_replyReturn->fullId() == id) {
			calculateNextReplyReturn();
		} else {
			break;
		}
	}
}

void CornerButtons::calculateNextReplyReturn() {
	_replyReturn = nullptr;
	while (!_replyReturns.empty() && !_replyReturn) {
		_replyReturns.pop_back();
		computeCurrentReplyReturn();
	}
	if (!_replyReturn) {
		updateJumpDownVisibility();
		updateUnreadThingsVisibility();
	}
}

void CornerButtons::pushReplyReturn(not_null<HistoryItem*> item) {
	_replyReturns.push_back(item->fullId());
	_replyReturn = item;

	if (!_replyReturnStarted) {
		_replyReturnStarted = true;
		item->history()->owner().itemRemoved(
		) | rpl::on_next([=](not_null<const HistoryItem*> item) {
			while (item == _replyReturn) {
				calculateNextReplyReturn();
			}
		}, _down.widget->lifetime());
	}
}

CornerButton &CornerButtons::buttonByType(Type type) {
	switch (type) {
	case Type::Down: return _down;
	case Type::Mentions: return _mentions;
	case Type::Reactions: return _reactions;
	case Type::PollVotes: return _pollVotes;
	case Type::Stash: return _stash;
	}
	Unexpected("Type in CornerButtons::buttonByType.");
}

History *CornerButtons::lookupHistory() const {
	const auto thread = _delegate->cornerButtonsThread();
	return thread ? thread->owningHistory().get() : nullptr;
}

void CornerButtons::showAt(MsgId id) {
	if (const auto history = lookupHistory()) {
		if (const auto item = history->owner().message(history->peer, id)) {
			_delegate->cornerButtonsShowAtPosition(item->position());
		}
	}
}

bool CornerButtons::ignoresVisibility() const {
	return _delegate->cornerButtonsIgnoreVisibility();
}

void CornerButtons::updateVisibility(Type type, bool shown) {
	auto &button = buttonByType(type);
	if (button.shown != shown) {
		button.shown = shown;
		if (type == Type::Stash) {
			_stashButton->setArrowShown(
				shown,
				button.animation.value(shown ? 0. : 1.));
		}
		button.animation.start(
			[=] { updatePositions(); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void CornerButtons::updateUnreadThingsVisibility() {
	if (_delegate->cornerButtonsIgnoreVisibility()) {
		return;
	}
	const auto thread = _delegate->cornerButtonsThread();
	if (!thread) {
		updateVisibility(Type::Mentions, false);
		updateVisibility(Type::Reactions, false);
		updateVisibility(Type::PollVotes, false);
		return;
	}
	auto &unreadThings = thread->session().api().unreadThings();
	unreadThings.preloadEnough(thread);

	const auto updateWithCount = [&](Type type, int count) {
		updateVisibility(
			type,
			(count > 0) && _delegate->cornerButtonsUnreadMayBeShown());
	};
	if (_delegate->cornerButtonsHas(Type::Mentions)
		&& unreadThings.trackMentions(thread)) {
		if (const auto count = thread->unreadMentions().count(0)) {
			_mentions.widget->setUnreadCount(count);
			updateAccessibleDescription(_mentions);
		}
		updateWithCount(
			Type::Mentions,
			thread->unreadMentions().loadedCount());
	} else {
		updateVisibility(Type::Mentions, false);
	}

	if (_delegate->cornerButtonsHas(Type::Reactions)
		&& unreadThings.trackReactions(thread)) {
		if (const auto count = thread->unreadReactions().count(0)) {
			_reactions.widget->setUnreadCount(count);
			updateAccessibleDescription(_reactions);
		}
		updateWithCount(
			Type::Reactions,
			thread->unreadReactions().loadedCount());
	} else {
		updateVisibility(Type::Reactions, false);
	}

	if (_delegate->cornerButtonsHas(Type::PollVotes)
		&& unreadThings.trackPollVotes(thread)) {
		if (const auto count = thread->unreadPollVotes().count(0)) {
			_pollVotes.widget->setUnreadCount(count);
			updateAccessibleDescription(_pollVotes);
		}
		updateWithCount(
			Type::PollVotes,
			thread->unreadPollVotes().loadedCount());
	} else {
		updateVisibility(Type::PollVotes, false);
	}
}

void CornerButtons::updateJumpDownVisibility(std::optional<int> counter) {
	if (const auto shown = _delegate->cornerButtonsDownShown()) {
		updateVisibility(Type::Down, *shown);
	}
	if (counter) {
		_down.widget->setUnreadCount(*counter);
		updateAccessibleDescription(_down);
	}
}

void CornerButtons::updatePositions() {
	const auto checkVisibility = [](CornerButton &button) {
		const auto shouldBeHidden = !button.shown
			&& !button.animation.animating();
		if (shouldBeHidden != button.widget->isHidden()) {
			button.widget->setVisible(!shouldBeHidden);
		}
	};
	const auto shown = [](CornerButton &button) {
		return button.animation.value(button.shown ? 1. : 0.);
	};

	// All corner buttons is a child widgets of _column over _scroll, not me.

	const auto columnWidth = st::historyToDown.width
		+ 2 * st::historyToDownPosition.x();
	_column.resize(columnWidth, _parent->height());
	_column.moveToRight(0, 0, _parent->width());

	const auto historyDownShown = shown(_down);
	const auto unreadMentionsShown = shown(_mentions);
	const auto unreadReactionsShown = shown(_reactions);
	const auto unreadPollVotesShown = shown(_pollVotes);
	const auto stashShown = shown(_stash);
	const auto skip = st::historyUnreadThingsSkip;
	{
		const auto top = anim::interpolate(
			0,
			_down.widget->height() + st::historyToDownPosition.y(),
			historyDownShown);
		_down.widget->moveToRight(
			st::historyToDownPosition.x(),
			_parent->height() - top);
	}
	{
		const auto right = anim::interpolate(
			-_mentions.widget->width(),
			st::historyToDownPosition.x(),
			unreadMentionsShown);
		const auto shift = anim::interpolate(
			0,
			_down.widget->height() + skip,
			historyDownShown);
		const auto top = _parent->height()
			- _mentions.widget->height()
			- st::historyToDownPosition.y()
			- shift;
		_mentions.widget->moveToRight(right, top);
	}
	{
		const auto right = anim::interpolate(
			-_reactions.widget->width(),
			st::historyToDownPosition.x(),
			unreadReactionsShown);
		const auto shift = anim::interpolate(
			0,
			_down.widget->height() + skip,
			historyDownShown
		) + anim::interpolate(
			0,
			_mentions.widget->height() + skip,
			unreadMentionsShown);
		const auto top = _parent->height()
			- _reactions.widget->height()
			- st::historyToDownPosition.y()
			- shift;
		_reactions.widget->moveToRight(right, top);
	}
	{
		const auto right = anim::interpolate(
			-_pollVotes.widget->width(),
			st::historyToDownPosition.x(),
			unreadPollVotesShown);
		const auto shift = anim::interpolate(
			0,
			_down.widget->height() + skip,
			historyDownShown
		) + anim::interpolate(
			0,
			_mentions.widget->height() + skip,
			unreadMentionsShown
		) + anim::interpolate(
			0,
			_reactions.widget->height() + skip,
			unreadReactionsShown);
		const auto top = _parent->height()
			- _pollVotes.widget->height()
			- st::historyToDownPosition.y()
			- shift;
		_pollVotes.widget->moveToRight(right, top);
	}
	{
		const auto right = anim::interpolate(
			-_stash.widget->width(),
			st::historyToDownPosition.x(),
			stashShown);
		const auto shift = anim::interpolate(
			0,
			_down.widget->height() + skip,
			historyDownShown
		) + anim::interpolate(
			0,
			_mentions.widget->height() + skip,
			unreadMentionsShown
		) + anim::interpolate(
			0,
			_reactions.widget->height() + skip,
			unreadReactionsShown
		) + anim::interpolate(
			0,
			_pollVotes.widget->height() + skip,
			unreadPollVotesShown);
		const auto top = _parent->height()
			- _stash.widget->height()
			- st::historyToDownPosition.y()
			- shift;
		_stash.widget->moveToRight(right, top);
	}

	checkVisibility(_down);
	checkVisibility(_mentions);
	checkVisibility(_reactions);
	checkVisibility(_pollVotes);
	checkVisibility(_stash);

	// Leave only the buttons in the column's hit test, so a click on the rest
	// of the strip goes to the list under it. The attribute alone would not
	// do - it drops the whole subtree out of the hit test, the buttons in it
	// included - but an empty region means "no mask" to Qt, not "nothing to
	// hit", so while there is no button to keep the column is made
	// transparent instead.
	auto mask = QRegion();
	const auto addToMask = [&](CornerButton &button) {
		if (!button.widget->isHidden()) {
			mask += button.widget->geometry();
		}
	};
	addToMask(_down);
	addToMask(_mentions);
	addToMask(_reactions);
	addToMask(_pollVotes);
	addToMask(_stash);
	_column.setAttribute(Qt::WA_TransparentForMouseEvents, mask.isEmpty());
	if (_columnMask != mask) {
		_columnMask = mask;
		_column.setMask(mask);
	}
}

void CornerButtons::finishAnimations() {
	_down.animation.stop();
	_mentions.animation.stop();
	_reactions.animation.stop();
	_pollVotes.animation.stop();
	_stash.animation.stop();
	_stashButton->finishAnimating();
	updatePositions();
}

rpl::producer<> CornerButtons::stashClicks() const {
	return _stashClicks.events();
}

void CornerButtons::setStashMenuFiller(
		Fn<void(not_null<Ui::PopupMenu*>)> filler) {
	_stashMenuFiller = std::move(filler);
}

void CornerButtons::showStashMenu() {
	if (!_stashMenuFiller) {
		return;
	}
	_stashMenu = base::make_unique_q<Ui::PopupMenu>(
		_stash.widget.data(),
		st::popupMenuWithIcons);
	_stashMenuFiller(_stashMenu.get());
	if (_stashMenu->empty()) {
		_stashMenu = nullptr;
		return;
	}
	const auto shadow = Ui::BoxShadow::ExtendFor(
		st::popupMenuWithIcons.shadow);
	_stashMenu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
	_stashMenu->popup(_stash.widget->mapToGlobal(
		st::historyStash.rippleAreaPosition
		+ QPoint(
			_stash.widget->width() - shadow.right(),
			-shadow.bottom())));
}

Fn<void(bool found)> CornerButtons::doneJumpFrom(
		FullMsgId targetId,
		FullMsgId originId,
		bool ignoreMessageNotFound) {
	return [=](bool found) {
		skipReplyReturn(targetId);
		if (originId) {
			if (const auto thread = _delegate->cornerButtonsThread()) {
				if (const auto item = thread->owner().message(originId)) {
					pushReplyReturn(item);
				}
			}
		}
		if (!found && !ignoreMessageNotFound) {
			Ui::Toast::Show(
				_parent.get(),
				tr::lng_message_not_found(tr::now));
		}
	};
}

} // namespace HistoryView
