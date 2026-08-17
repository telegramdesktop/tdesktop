/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_top_controls.h"

#include "apiwrap.h"
#include "base/binary_guard.h"
#include "base/call_delayed.h"
#include "boxes/peers/edit_peer_requests_box.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "data/components/sponsored_messages.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_shared_media.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_group_call_bar.h"
#include "history/view/history_view_pinned_bar.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_requests_bar.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/history_view_translate_tracker.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "ui/chat/group_call_bar.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/sponsored_message_bar.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace HistoryView {
namespace {

rpl::producer<Ui::MessageBarContent> RootViewContent(
		not_null<History*> history,
		MsgId rootId,
		Fn<void()> repaint) {
	return MessageBarContentByItemId(
		&history->session(),
		FullMsgId(history->peer->id, rootId),
		std::move(repaint)
	) | rpl::map([=](Ui::MessageBarContent &&content) {
		const auto item = history->owner().message(history->peer, rootId);
		if (!item) {
			content.text = tr::link(tr::lng_deleted_message(tr::now));
		}
		PeerData *sender = history->peer;
		if (item && item->discussionPostOriginalSender()) {
			sender = item->discussionPostOriginalSender();
		}
		content.title = sender->name().isEmpty()
			? "Message"
			: sender->name();
		return std::move(content);
	});
}

} // namespace

TopControls::TopControls(
	not_null<Ui::RpWidget*> parent,
	TopControlsDescriptor descriptor)
: _parent(parent)
, _controller(descriptor.controller)
, _history(descriptor.history)
, _repliesRootId(descriptor.repliesRootId)
, _topic(descriptor.topic)
, _sublist(descriptor.sublist)
, _monoforumPeerId(descriptor.monoforumPeerId)
, _scroll(descriptor.scroll)
, _list(descriptor.list)
, _keyboardReservedHeight(std::move(descriptor.keyboardReservedHeight))
, _showAtStart(std::move(descriptor.showAtStart))
, _showAtPosition(std::move(descriptor.showAtPosition))
, _preparePinnedClickContext(std::move(descriptor.preparePinnedClickContext))
, _wrap(std::make_unique<Ui::RpWidget>(_parent))
, _topBars(std::make_unique<Ui::RpWidget>(_wrap.get())) {
	_topBars->show();
	_wrap->show();
	setupTranslateBar();
	rebuildModeSensitiveBars();
	requestSponsoredMessageBar();
	rpl::merge(
		_list->heightValue() | rpl::to_empty,
		_scroll->heightValue() | rpl::to_empty
	) | rpl::on_next([=] {
		checkSponsoredMessageBar();
	}, _wrap->lifetime());
	updateLayout();

	_moveWithTopDelta = std::move(descriptor.moveWithTopDelta);
	_relayout = std::move(descriptor.relayout);
	_relayoutWithScrollTopDelta = std::move(
		descriptor.relayoutWithScrollTopDelta);
}

TopControls::~TopControls() = default;

void TopControls::move(int x, int y) {
	_wrap->move(x, y);
}

void TopControls::resizeToWidth(int width) {
	if (_width == width) {
		return;
	}
	_width = width;
	updateLayout();
}

void TopControls::raise() {
	_wrap->raise();
	updateZOrder();
}

void TopControls::show() {
	_wrap->show();
	updateZOrder();
}

void TopControls::hide() {
	_wrap->hide();
}

void TopControls::subscribeToPinnedMessages() {
	const auto thread = activeThread();
	Expects(thread != nullptr);
	const auto migrated = migratedPeer()
		? _history->migrateFrom()
		: nullptr;

	const auto was = height();
	resetPinnedState();
	updateLayout();
	applyHeightChangeWithRelayout(was, height());

	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	_history->session().changes().entryUpdates(
		EntryUpdateFlag::HasPinnedMessages
	) | rpl::on_next([=](const Data::EntryUpdate &update) {
		if (_pinnedTracker
			&& (update.flags & EntryUpdateFlag::HasPinnedMessages)
			&& ((thread == update.entry.get())
				|| (migrated == update.entry.get()))) {
			checkPinnedBarState();
		}
	}, _pinnedLifetime);

	setupPinnedTracker();
}

void TopControls::setAnimatingMode(bool enabled) {
	_animatingMode = enabled;
}

void TopControls::finishAnimating() {
	checkSponsoredMessageBar();
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_hidingPinnedBar) {
		_hidingPinnedBar->finishAnimating();
	}
	if (!_animatingMode) {
		return;
	}
	_animatingMode = false;
}

void TopControls::setRepliesRootId(MsgId repliesRootId) {
	if (_repliesRootId == repliesRootId) {
		return;
	}
	const auto was = height();
	_repliesRootId = repliesRootId;
	resetPinnedState();
	_repliesRootView = nullptr;
	_repliesRootViewHeight = 0;
	_repliesRootViewInited = false;
	_repliesRootViewInitScheduled = false;
	_shownRepliesRootItem = nullptr;
	rebuildModeSensitiveBars();
	updateLayout();
	applyHeightChangeWithRelayout(was, height());
}

void TopControls::setTopic(Data::ForumTopic *topic) {
	if (_topic == topic) {
		return;
	}
	const auto was = height();
	_topic = topic;
	resetPinnedState();
	_repliesRootView = nullptr;
	_repliesRootViewHeight = 0;
	_repliesRootViewInited = false;
	_repliesRootViewInitScheduled = false;
	_topicReopenBar = nullptr;
	_topicReopenBarHeight = 0;
	_shownRepliesRootItem = nullptr;
	rebuildModeSensitiveBars();
	updateLayout();
	applyHeightChangeWithRelayout(was, height());
}

Data::Thread *TopControls::activeThread() const {
	return _sublist
		? static_cast<Data::Thread*>(_sublist)
		: _topic
		? static_cast<Data::Thread*>(_topic)
		: static_cast<Data::Thread*>(_history.get());
}

PeerData *TopControls::migratedPeer() const {
	if (_repliesRootId || _topic || _sublist) {
		return nullptr;
	}
	const auto migrated = _history->migrateFrom();
	return migrated ? migrated->peer.get() : nullptr;
}

bool TopControls::showInForum() const {
	const auto thread = activeThread();
	return thread && thread->topicRootId();
}

void TopControls::setRepliesRootVisible(bool shown) {
	if (_animatingMode || (_repliesRootVisible.current() == shown)) {
		return;
	} else if (_sublist) {
		_repliesRootVisible = shown;
		return;
	} else if (!_repliesRootId) {
		return;
	} else if (!_topic) {
		if (!_repliesRootViewInitScheduled) {
			const auto next = shown ? st::historyReplyHeight : 0;
			if (next != _repliesRootViewHeight) {
				const auto was = height();
				_repliesRootViewHeight = next;
				updateLayout();
				applyHeightChangeWithTopMoved(was, height());
			}
		}
		_repliesRootVisible = shown;
		if (_repliesRootView && !_repliesRootViewInited) {
			_repliesRootView->finishAnimating();
			if (!_repliesRootViewInitScheduled) {
				_repliesRootViewInitScheduled = true;
				base::call_delayed(0, _wrap.get(), [=] {
					_repliesRootViewInited = true;
				});
			}
		}
	} else {
		_repliesRootVisible = shown;
	}
}

rpl::producer<bool> TopControls::repliesRootVisibleValue() const {
	return _repliesRootVisible.value();
}

void TopControls::updatePinnedViewer() {
	if (_scroll->isHidden() || !_pinnedTracker) {
		return;
	}
	const auto migrated = migratedPeer();
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	auto [view, offset] = _list->findViewForPinnedTracking(visibleBottom);
	const auto lessThanId = !view
		? (ServerMaxMsgId - 1)
		: (migrated && (view->history() != _history))
		? (view->data()->id + (offset > 0 ? 1 : 0) - ServerMaxMsgId)
		: (view->data()->id + (offset > 0 ? 1 : 0));
	const auto lastClickedId = !_pinnedClickedId
		? (ServerMaxMsgId - 1)
		: (migrated && !peerIsChannel(_pinnedClickedId.peer))
		? (_pinnedClickedId.msg - ServerMaxMsgId)
		: _pinnedClickedId.msg;
	if (_pinnedClickedId
		&& lessThanId <= lastClickedId
		&& !_list->animatedScrolling()) {
		_pinnedClickedId = FullMsgId();
	}
	if (_pinnedClickedId && !_minPinnedId) {
		const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
		_minPinnedId = Data::ResolveMinPinnedId(
			_history->peer,
			fullHistory ? MsgId(0) : _repliesRootId,
			fullHistory ? PeerId(0) : _monoforumPeerId,
			migrated);
	}
	if (_pinnedClickedId && _minPinnedId && _minPinnedId >= _pinnedClickedId) {
		_pinnedTracker->trackAround(ServerMaxMsgId - 1);
	} else {
		_pinnedTracker->trackAround(std::min(lessThanId, lastClickedId));
	}
}

void TopControls::checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop) {
	if (_scroll->isHidden() || !_pinnedTracker) {
		return;
	}
	if (wasScrollTop < nowScrollTop && _pinnedClickedId) {
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}
}

void TopControls::addTranslatedItems(not_null<TranslateTracker*> tracker) {
	if (_shownRepliesRootItem) {
		tracker->add(_shownRepliesRootItem);
	}
	if (_shownPinnedBarItem) {
		tracker->add(_shownPinnedBarItem);
	}
}

int TopControls::height() const {
	return _height.current();
}

rpl::producer<int> TopControls::heightValue() const {
	return _height.value();
}

void TopControls::setupRootView() {
	_repliesRootView = std::make_unique<Ui::PinnedBar>(_topBars.get(), [=] {
		return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	}, _controller->gifPauseLevelChanged());
	_repliesRootView->setContent(rpl::combine(
		RootViewContent(
			_history,
			_repliesRootId,
			[bar = _repliesRootView.get()] { bar->customEmojiRepaint(); }),
		_repliesRootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, bool show) {
		const auto shown = !content.title.isEmpty() && !content.text.empty();
		crl::on_main(_wrap.get(), [=] {
			_shownRepliesRootItem = shown
				? _history->owner().message(_history->peer->id, _repliesRootId)
				: nullptr;
		});
		return show ? std::move(content) : Ui::MessageBarContent();
	}));

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_repliesRootView->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _repliesRootView->lifetime());

	_repliesRootView->barClicks(
	) | rpl::on_next([=] {
		if (_showAtStart) {
			_showAtStart();
		}
	}, _repliesRootView->lifetime());

	_repliesRootViewHeight = 0;
	_repliesRootView->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _repliesRootViewHeight) {
			return;
		}
		const auto was = this->height();
		_repliesRootViewHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _repliesRootView->lifetime());
}

void TopControls::setupTopicReopenBar() {
	_topicReopenBar = std::make_unique<TopicReopenBar>(_topBars.get(), _topic);
	_topicReopenBar->bar().setVisible(true);
	_topicReopenBarHeight = _topicReopenBar->bar().height();
	_topicReopenBar->bar().heightValue(
	) | rpl::on_next([=] {
		const auto height = _topicReopenBar->bar().height();
		if (height == _topicReopenBarHeight) {
			return;
		}
		const auto was = this->height();
		_topicReopenBarHeight = height;
		updateLayout();
		const auto delta = this->height() - was;
		if (!delta) {
			return;
		} else if (_relayoutWithScrollTopDelta) {
			_relayoutWithScrollTopDelta(delta);
		} else {
			applyHeightChangeWithRelayout(was, this->height());
		}
	}, _topicReopenBar->bar().lifetime());
}

void TopControls::setupTranslateBar() {
	_translateBar = std::make_unique<TranslateBar>(
		_topBars.get(),
		_controller,
		_history);
	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=, raw = _translateBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _translateBar->lifetime());

	_translateBarHeight = 0;
	_translateBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _translateBarHeight) {
			return;
		}
		const auto was = this->height();
		_translateBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _translateBar->lifetime());

	_translateBar->finishAnimating();
}

void TopControls::setupGroupCallBar() {
	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		return;
	}
	_groupCallBar = std::make_unique<Ui::GroupCallBar>(
		_topBars.get(),
		GroupCallBarContentByPeer(
			peer,
			st::historyGroupCallUserpics.size,
			showInForum()),
		Core::App().appDeactivatedValue());

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_groupCallBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _groupCallBar->lifetime());

	rpl::merge(
		_groupCallBar->barClicks(),
		_groupCallBar->joinClicks()
	) | rpl::on_next([=] {
		if (const auto peer = _history->peer; peer->groupCall()) {
			_controller->startOrJoinGroupCall(peer, {});
		}
	}, _groupCallBar->lifetime());

	_groupCallBarHeight = 0;
	_groupCallBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _groupCallBarHeight) {
			return;
		}
		const auto was = this->height();
		_groupCallBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _groupCallBar->lifetime());
}

void TopControls::setupRequestsBar() {
	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		return;
	}
	_requestsBar = std::make_unique<Ui::RequestsBar>(
		_topBars.get(),
		RequestsBarContentByPeer(
			peer,
			st::historyRequestsUserpics.size,
			showInForum()));

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_requestsBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _requestsBar->lifetime());

	_requestsBar->barClicks(
	) | rpl::on_next([=] {
		RequestsBoxController::Start(_controller, _history->peer);
	}, _requestsBar->lifetime());

	_requestsBarHeight = 0;
	_requestsBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _requestsBarHeight) {
			return;
		}
		const auto was = this->height();
		_requestsBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _requestsBar->lifetime());
}

void TopControls::setupPeerBars() {
	if (!_contactStatus) {
		_contactStatus = std::make_unique<ContactStatus>(
			_controller,
			_topBars.get(),
			_history->peer,
			showInForum());
		_contactStatus->show();
		_contactStatusHeight = 0;
		_contactStatus->bar().heightValue(
		) | rpl::on_next([=] {
			const auto height = _contactStatus->bar().height();
			if (height == _contactStatusHeight) {
				return;
			}
			const auto was = this->height();
			_contactStatusHeight = height;
			updateLayout();
			applyHeightChangeWithRelayout(was, this->height());
		}, _contactStatus->bar().lifetime());
	}
	const auto user = _history->peer->asUser();
	if (!user) {
		return;
	}
	if (!_paysStatus) {
		_paysStatus = std::make_unique<PaysStatus>(
			_controller,
			_topBars.get(),
			user);
		_paysStatus->show();
		_paysStatusHeight = 0;
		_paysStatus->bar().heightValue(
		) | rpl::on_next([=] {
			const auto height = _paysStatus->bar().height();
			if (height == _paysStatusHeight) {
				return;
			}
			const auto was = this->height();
			_paysStatusHeight = height;
			updateLayout();
			applyHeightChangeWithRelayout(was, this->height());
		}, _paysStatus->bar().lifetime());
	}
	if (!_businessBotStatus) {
		_businessBotStatus = std::make_unique<BusinessBotStatus>(
			_controller,
			_topBars.get(),
			_history->peer);
		_businessBotStatus->show();
		_businessBotStatusHeight = 0;
		_businessBotStatus->bar().heightValue(
		) | rpl::on_next([=] {
			const auto height = _businessBotStatus->bar().height();
			if (height == _businessBotStatusHeight) {
				return;
			}
			const auto was = this->height();
			_businessBotStatusHeight = height;
			updateLayout();
			applyHeightChangeWithRelayout(was, this->height());
		}, _businessBotStatus->bar().lifetime());
	}
}

bool TopControls::checkSponsoredMessageBarVisibility() const {
	const auto reserved = _keyboardReservedHeight
		? _keyboardReservedHeight()
		: 0;
	return (_list->height() - reserved > _scroll->height());
}

void TopControls::requestSponsoredMessageBar() {
	if (!_history->session().sponsoredMessages().isTopBarFor(_history)) {
		return;
	}
	const auto checkState = [=] {
		using State = Data::SponsoredMessages::State;
		if (_history->session().sponsoredMessages().state(_history)
			!= State::AppendToTopBar) {
			return;
		}
		createSponsoredMessageBar();
		if (checkSponsoredMessageBarVisibility()) {
			_sponsoredMessageBar->toggle(true, anim::type::normal);
		} else {
			auto &lifetime = _sponsoredMessageBar->lifetime();
			const auto heightLifetime = lifetime.make_state<rpl::lifetime>();
			_list->heightValue(
			) | rpl::on_next([=] {
				if (_sponsoredMessageBar->toggled()) {
					heightLifetime->destroy();
				} else if (checkSponsoredMessageBarVisibility()) {
					_sponsoredMessageBar->toggle(true, anim::type::normal);
					heightLifetime->destroy();
				}
			}, *heightLifetime);
		}
	};
	const auto history = _history;
	_history->session().sponsoredMessages().request(
		_history,
		crl::guard(_wrap.get(), [=] {
			if (history == _history) {
				checkState();
			}
		}));
}

void TopControls::checkSponsoredMessageBar() {
	using State = Data::SponsoredMessages::State;
	if (!_history->session().sponsoredMessages().isTopBarFor(_history)) {
		return;
	}
	if (_history->session().sponsoredMessages().state(_history)
		!= State::AppendToTopBar) {
		return;
	}
	if (checkSponsoredMessageBarVisibility()) {
		createSponsoredMessageBar();
		_sponsoredMessageBar->toggle(true, anim::type::instant);
	}
}

void TopControls::createSponsoredMessageBar() {
	if (_sponsoredMessageBar) {
		return;
	}
	_sponsoredMessageBar = base::make_unique_q<Ui::SlideWrap<Ui::RpWidget>>(
		_topBars.get(),
		object_ptr<Ui::RpWidget>(_topBars.get()));

	_sponsoredMessageBar->entity()->resizeToWidth(_width);
	const auto maybeFullId = _history->session().sponsoredMessages().fillTopBar(
		_history,
		_sponsoredMessageBar->entity());
	_history->session().sponsoredMessages().itemRemoved(
		maybeFullId
	) | rpl::on_next([this] {
		_sponsoredMessageBar->toggle(false, anim::type::normal);
		_sponsoredMessageBar->shownValue() | rpl::filter(
			!rpl::mappers::_1
		) | rpl::on_next([this] {
			_sponsoredMessageBar = nullptr;
		}, _sponsoredMessageBar->lifetime());
	}, _sponsoredMessageBar->lifetime());

	if (maybeFullId) {
		const auto viewLifetime
			= _sponsoredMessageBar->lifetime().make_state<rpl::lifetime>();
		rpl::combine(
			_sponsoredMessageBar->entity()->heightValue(),
			_sponsoredMessageBar->heightValue()
		) | rpl::filter(
			rpl::mappers::_1 == rpl::mappers::_2
		) | rpl::on_next([=] {
			_history->session().sponsoredMessages().view(maybeFullId);
			viewLifetime->destroy();
		}, *viewLifetime);
	}

	_sponsoredMessageBarHeight = 0;
	_sponsoredMessageBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _sponsoredMessageBarHeight) {
			return;
		}
		const auto was = this->height();
		_sponsoredMessageBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _sponsoredMessageBar->lifetime());
	_sponsoredMessageBar->toggle(false, anim::type::instant);
	updateZOrder();
}

void TopControls::setupPinnedTracker() {
	const auto thread = activeThread();
	Expects(thread != nullptr);

	_pinnedTracker = std::make_unique<PinnedTracker>(thread);
	_pinnedBar = nullptr;
	const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
	if (fullHistory) {
		checkPinnedBarState();
		return;
	}

	SharedMediaViewer(
		&_history->session(),
		Storage::SharedMediaKey(
			_history->peer->id,
			_repliesRootId,
			_monoforumPeerId,
			Storage::SharedMediaType::Pinned,
			ServerMaxMsgId - 1),
		1,
		1
	) | rpl::filter([=](const SparseIdsSlice &result) {
		return result.fullCount().has_value();
	}) | rpl::on_next([=](const SparseIdsSlice &result) {
		thread->setHasPinnedMessages(*result.fullCount() != 0);
		if (result.skippedAfter() == 0) {
			auto &settings = _history->session().settings();
			const auto peerId = _history->peer->id;
			const auto hiddenId = settings.hiddenPinnedMessageId(
				peerId,
				_repliesRootId,
				_monoforumPeerId);
			const auto last = result.size() ? result[result.size() - 1] : 0;
			if (hiddenId && hiddenId != last) {
				settings.setHiddenPinnedMessageId(
					peerId,
					_repliesRootId,
					_monoforumPeerId,
					0);
				_history->session().saveSettingsDelayed();
			}
		}
		checkPinnedBarState();
	}, _pinnedLifetime);
}

void TopControls::checkPinnedBarState() {
	Expects(_pinnedTracker != nullptr);

	const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
	const auto migrated = migratedPeer();
	const auto topicRootId = fullHistory ? MsgId(0) : _repliesRootId;
	const auto monoforumPeerId = fullHistory ? PeerId(0) : _monoforumPeerId;
	const auto hiddenId = _history->peer->canPinMessages()
		? MsgId(0)
		: _history->peer->session().settings().hiddenPinnedMessageId(
			_history->peer->id,
			topicRootId,
			monoforumPeerId);
	const auto currentPinnedId = Data::ResolveTopPinnedId(
		_history->peer,
		topicRootId,
		monoforumPeerId,
		migrated);
	const auto universalPinnedId = !currentPinnedId
		? MsgId(0)
		: (migrated && !peerIsChannel(currentPinnedId.peer))
		? (currentPinnedId.msg - ServerMaxMsgId)
		: currentPinnedId.msg;
	if (universalPinnedId == hiddenId) {
		if (_pinnedBar) {
			_pinnedBar->setContent(rpl::single(Ui::MessageBarContent()));
			_pinnedTracker->reset();
			_shownPinnedBarItem = nullptr;
			_hidingPinnedBar = std::move(_pinnedBar);
			updateLayout();
			updateZOrder();
			const auto raw = _hidingPinnedBar.get();
			base::call_delayed(st::defaultMessageBar.duration, _wrap.get(), [=] {
				if (_hidingPinnedBar.get() == raw) {
					clearHidingPinnedBar();
				}
			});
		}
		return;
	}
	if (_pinnedBar || !universalPinnedId) {
		return;
	}

	clearHidingPinnedBar();
	_pinnedBar = std::make_unique<Ui::PinnedBar>(_topBars.get(), [=] {
		return _controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, _controller->gifPauseLevelChanged());
	auto pinnedRefreshed = Info::Profile::SharedMediaCountValue(
		_history->peer,
		topicRootId,
		monoforumPeerId,
		migrated,
		Storage::SharedMediaType::Pinned
	) | rpl::distinct_until_changed(
	) | rpl::map([=](int count) {
		if (_pinnedClickedId) {
			_pinnedClickedId = FullMsgId();
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
		return (count > 1);
	}) | rpl::distinct_until_changed();
	auto customButtonItem = PinnedBarItemWithCustomButton(
		&_history->session(),
		_pinnedTracker->shownMessageId());
	rpl::combine(
		rpl::duplicate(pinnedRefreshed),
		rpl::duplicate(customButtonItem)
	) | rpl::on_next([=](bool many, HistoryItem *item) {
		refreshPinnedBarButton(many, item);
	}, _pinnedBar->lifetime());

	_pinnedBar->setContent(rpl::combine(
		PinnedBarContent(
			&_history->session(),
			_pinnedTracker->shownMessageId(),
			[bar = _pinnedBar.get()] { bar->customEmojiRepaint(); }),
		std::move(pinnedRefreshed),
		std::move(customButtonItem),
		_repliesRootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, auto, auto, bool show) {
		const auto shown = !content.title.isEmpty() && !content.text.empty();
		const auto id = shown
			? _pinnedTracker->currentMessageId().message
			: FullMsgId();
		crl::on_main(_wrap.get(), [=] {
			_shownPinnedBarItem = id
				? _history->owner().message(id)
				: nullptr;
		});
		return (fullHistory || show || content.count > 1)
			? std::move(content)
			: Ui::MessageBarContent();
	}));

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=, raw = _pinnedBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _pinnedBar->lifetime());

	_pinnedBar->barClicks(
	) | rpl::on_next([=] {
		const auto id = _pinnedTracker->currentMessageId();
		if (const auto item = _history->session().data().message(id.message)) {
			if (_showAtPosition) {
				_showAtPosition(item->position());
			}
			if (const auto group = _history->session().data().groups().find(item)) {
				_pinnedClickedId = group->items.front()->fullId();
			} else {
				_pinnedClickedId = id.message;
			}
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
	}, _pinnedBar->lifetime());

	_pinnedBar->barRightClicks(
	) | rpl::on_next([=] {
		if (_pinnedBarHasCustomButton) {
			return;
		}
		const auto reference = _pinnedClickedId
			? _pinnedClickedId
			: _pinnedTracker->currentMessageId().message;
		if (!reference) {
			return;
		}
		const auto migrated = migratedPeer();
		const auto universal = [&](FullMsgId id) {
			return (!id || !migrated || peerIsChannel(id.peer))
				? id.msg
				: (id.msg - ServerMaxMsgId);
		};
		const auto referenceId = universal(reference);
		const auto top = Data::ResolveTopPinnedId(
			_history->peer,
			topicRootId,
			monoforumPeerId,
			migrated);
		const auto targetId = (top && referenceId >= universal(top))
			? Data::ResolveMinPinnedId(
				_history->peer,
				topicRootId,
				monoforumPeerId,
				migrated)
			: _pinnedTracker->nextPinnedId(referenceId);
		if (!targetId) {
			return;
		}
		const auto jump = crl::guard(_wrap.get(), [=] {
			const auto item = _history->owner().message(targetId);
			if (!item) {
				return;
			}
			if (_showAtPosition) {
				_showAtPosition(item->position());
			}
			_pinnedClickedId = FullMsgId();
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		});
		if (_history->owner().message(targetId)) {
			jump();
		} else {
			_history->session().api().requestMessageData(
				_history->owner().peer(targetId.peer),
				targetId.msg,
				jump);
		}
	}, _pinnedBar->lifetime());

	_pinnedBarHeight = 0;
	_pinnedBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _pinnedBarHeight) {
			return;
		}
		const auto was = this->height();
		_pinnedBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _pinnedBar->lifetime());

	updateLayout();
	updateZOrder();
}

void TopControls::clearHidingPinnedBar() {
	if (!_hidingPinnedBar) {
		return;
	}
	const auto was = height();
	_pinnedBarHeight = 0;
	_hidingPinnedBar = nullptr;
	updateLayout();
	applyHeightChangeWithTopMoved(was, height());
}

void TopControls::refreshPinnedBarButton(bool many, HistoryItem *item) {
	if (!_pinnedBar) {
		return;
	}
	const auto openSection = [=] {
		const auto id = _pinnedTracker
			? _pinnedTracker->currentMessageId()
			: PinnedId();
		if (!id.message) {
			return;
		}
		if (!_repliesRootId && !_topic && !_sublist) {
			const auto normalizedMsgId = (migratedPeer()
					&& !peerIsChannel(id.message.peer))
				? (id.message.msg - ServerMaxMsgId)
				: id.message.msg;
			_controller->showSection(
				std::make_shared<PinnedMemento>(_history, normalizedMsgId));
			return;
		}
		if (const auto thread = activeThread()) {
			_controller->showSection(
				std::make_shared<PinnedMemento>(thread, id.message.msg));
		}
	};
	const auto context = [=](FullMsgId itemId) {
		return _preparePinnedClickContext
			? _preparePinnedClickContext(itemId)
			: ClickHandlerContext();
	};
	auto customButton = CreatePinnedBarCustomButton(_wrap.get(), item, context);
	if (customButton) {
		_pinnedBarHasCustomButton = true;
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
		};
		const auto buttonRaw = customButton.data();
		const auto state = buttonRaw->lifetime().make_state<State>();
		_pinnedBar->contextMenuRequested(
		) | rpl::on_next([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(buttonRaw);
			state->menu->addAction(
				tr::lng_settings_events_pinned(tr::now),
				openSection);
			state->menu->popup(QCursor::pos());
		}, buttonRaw->lifetime());
		_pinnedBar->setRightButton(std::move(customButton));
		return;
	}
	_pinnedBarHasCustomButton = false;
	const auto close = !many;
	auto button = object_ptr<Ui::IconButton>(
		_wrap.get(),
		close ? st::historyReplyCancel : st::historyPinnedShowAll);
	button->setAccessibleName(close
		? tr::lng_pinned_unpin(tr::now)
		: tr::lng_settings_events_pinned(tr::now));
	button->clicks(
	) | rpl::on_next([=] {
		if (close) {
			hidePinnedMessage();
		} else {
			openSection();
		}
	}, button->lifetime());
	_pinnedBar->setRightButton(std::move(button));
}

void TopControls::hidePinnedMessage() {
	Expects(_pinnedBar != nullptr);

	const auto id = _pinnedTracker->currentMessageId();
	if (!id.message) {
		return;
	}
	if (_history->peer->canPinMessages()) {
		Window::ToggleMessagePinned(_controller, id.message, false);
	} else {
		const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
		Window::HidePinnedBar(
			_controller,
			_history->peer,
			fullHistory ? MsgId(0) : _repliesRootId,
			fullHistory ? PeerId(0) : _monoforumPeerId,
			crl::guard(_wrap.get(), [=] {
				if (_pinnedTracker) {
					checkPinnedBarState();
				}
			}));
	}
}

void TopControls::resetPinnedState() {
	_pinnedLifetime.destroy();
	_pinnedTracker = nullptr;
	_pinnedBar = nullptr;
	_hidingPinnedBar = nullptr;
	_pinnedBarHeight = 0;
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;
	_shownPinnedBarItem = nullptr;
}

void TopControls::rebuildModeSensitiveBars() {
	const auto inForum = showInForum();
	const auto fullChat = !_repliesRootId && !_sublist;
	if (!_modeSensitiveBarsInited
		|| (_modeSensitiveShowInForum != inForum)
		|| (_modeSensitiveFullChat != fullChat)) {
		_modeSensitiveBarsInited = true;
		_modeSensitiveShowInForum = inForum;
		_modeSensitiveFullChat = fullChat;
		_groupCallBar = nullptr;
		_requestsBar = nullptr;
		_contactStatus = nullptr;
		_paysStatus = nullptr;
		_businessBotStatus = nullptr;
		_groupCallBarHeight = 0;
		_requestsBarHeight = 0;
		_contactStatusHeight = 0;
		_paysStatusHeight = 0;
		_businessBotStatusHeight = 0;
		if (fullChat) {
			setupGroupCallBar();
			setupRequestsBar();
		}
	}
	if (fullChat) {
		setupPeerBars();
	}
	if (_topic || !_repliesRootId) {
		if (_repliesRootView) {
			_repliesRootView = nullptr;
			_repliesRootViewHeight = 0;
			_repliesRootViewInited = false;
			_repliesRootViewInitScheduled = false;
			_shownRepliesRootItem = nullptr;
		}
	} else if (!_repliesRootView) {
		setupRootView();
	}
	if (_topic) {
		if (!_topicReopenBar) {
			setupTopicReopenBar();
		}
	} else if (_topicReopenBar) {
		_topicReopenBar = nullptr;
		_topicReopenBarHeight = 0;
	}
	updateZOrder();
}

void TopControls::updateLayout() {
	auto top = 0;
	const auto pinnedBar = _pinnedBar
		? _pinnedBar.get()
		: _hidingPinnedBar.get();
	_topBars->move(0, 0);
	if (_repliesRootView) {
		_repliesRootView->move(0, top);
		_repliesRootView->resizeToWidth(_width);
		top += _repliesRootViewHeight;
	}
	if (_groupCallBar) {
		_groupCallBar->move(0, top);
		_groupCallBar->resizeToWidth(_width);
		top += _groupCallBarHeight;
	}
	if (_requestsBar) {
		_requestsBar->move(0, top);
		_requestsBar->resizeToWidth(_width);
		top += _requestsBarHeight;
	}
	if (pinnedBar) {
		pinnedBar->move(0, top);
		pinnedBar->resizeToWidth(_width);
		top += _pinnedBarHeight;
	}
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->move(0, top);
		_sponsoredMessageBar->resizeToWidth(_width);
		top += _sponsoredMessageBarHeight;
	}
	if (_topicReopenBar) {
		_topicReopenBar->bar().move(0, top);
		top += _topicReopenBarHeight;
	}
	if (_translateBar) {
		_translateBar->move(0, top);
		_translateBar->resizeToWidth(_width);
		top += _translateBarHeight;
	}
	if (_paysStatus) {
		_paysStatus->bar().move(0, top);
		top += _paysStatusHeight;
	}
	if (_contactStatus) {
		_contactStatus->bar().move(0, top);
		top += _contactStatusHeight;
	}
	if (_businessBotStatus) {
		_businessBotStatus->bar().move(0, top);
		top += _businessBotStatusHeight;
	}
	_topBars->resize(_width, top + st::lineWidth);
	_wrap->resize(_width, top + st::lineWidth);
	_height = top;
}

void TopControls::updateZOrder() {
	const auto pinnedBar = _pinnedBar
		? _pinnedBar.get()
		: _hidingPinnedBar.get();
	_topBars->raise();
	if (_businessBotStatus) {
		_businessBotStatus->bar().raise();
	}
	if (_contactStatus) {
		_contactStatus->bar().raise();
	}
	if (_paysStatus) {
		_paysStatus->bar().raise();
	}
	if (_translateBar) {
		_translateBar->raise();
	}
	if (_topicReopenBar) {
		_topicReopenBar->bar().raise();
	}
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->raise();
	}
	if (pinnedBar) {
		pinnedBar->raise();
	}
	if (_requestsBar) {
		_requestsBar->raise();
	}
	if (_groupCallBar) {
		_groupCallBar->raise();
	}
	if (_repliesRootView) {
		_repliesRootView->raise();
	}
}

void TopControls::applyHeightChange(int was, int now, bool preserveTop) {
	if (was == now) {
		return;
	} else if (preserveTop) {
		applyHeightChangeWithTopMoved(was, now);
	} else {
		applyHeightChangeWithRelayout(was, now);
	}
}

void TopControls::applyHeightChangeWithTopMoved(int was, int now) {
	const auto delta = now - was;
	if (!delta) {
		return;
	} else if (_moveWithTopDelta) {
		_moveWithTopDelta(delta);
	} else if (_relayout) {
		_relayout();
	}
}

void TopControls::applyHeightChangeWithRelayout(int was, int now) {
	if (was == now) {
		return;
	} else if (_relayout) {
		_relayout();
	} else if (_moveWithTopDelta) {
		_moveWithTopDelta(now - was);
	}
}

} // namespace HistoryView
