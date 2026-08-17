/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/required.h"
#include "core/click_handler_types.h"
#include "data/data_messages.h"
#include "ui/rp_widget.h"

#include <optional>

class History;
class HistoryItem;
class PeerData;

namespace Data {
class ForumTopic;
class SavedSublist;
class Thread;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
template <typename Widget>
class SlideWrap;
class GroupCallBar;
class PinnedBar;
class RequestsBar;
class ElasticScroll;
} // namespace Ui

namespace HistoryView {

class BusinessBotStatus;
class ContactStatus;
class ListWidget;
class PaysStatus;
class PinnedTracker;
class TopicReopenBar;
class TranslateTracker;
class TranslateBar;

struct TopControlsDescriptor {
	Window::SessionController *controller = nullptr;
	History *history = nullptr;
	MsgId repliesRootId = 0;
	Data::ForumTopic *topic = nullptr;
	Data::SavedSublist *sublist = nullptr;
	PeerId monoforumPeerId = 0;
	Ui::ElasticScroll *scroll = nullptr;
	ListWidget *list = nullptr;
	Fn<int()> keyboardReservedHeight;
	Fn<void(int)> moveWithTopDelta;
	Fn<void()> relayout;
	Fn<void(int)> relayoutWithScrollTopDelta;
	Fn<void()> showAtStart;
	Fn<void(Data::MessagePosition)> showAtPosition;
	Fn<ClickHandlerContext(FullMsgId)> preparePinnedClickContext;
};

class TopControls final {
public:
	TopControls(
		not_null<Ui::RpWidget*> parent,
		TopControlsDescriptor descriptor);
	~TopControls();

	void move(int x, int y);
	void resizeToWidth(int width);
	void raise();
	void show();
	void hide();
	void subscribeToPinnedMessages();
	void setAnimatingMode(bool enabled);
	void finishAnimating();
	void setRepliesRootId(MsgId repliesRootId);
	void setTopic(Data::ForumTopic *topic);
	void setRepliesRootVisible(bool shown);
	[[nodiscard]] rpl::producer<bool> repliesRootVisibleValue() const;
	void updatePinnedViewer();
	void checkLastPinnedClickedIdReset(int wasScrollTop, int nowScrollTop);
	void addTranslatedItems(not_null<TranslateTracker*> tracker);

	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

private:
	[[nodiscard]] Data::Thread *activeThread() const;
	[[nodiscard]] PeerData *migratedPeer() const;
	[[nodiscard]] bool showInForum() const;
	void setupRootView();
	void setupTopicReopenBar();
	void setupTranslateBar();
	void setupGroupCallBar();
	void setupRequestsBar();
	void setupPeerBars();
	void requestSponsoredMessageBar();
	void checkSponsoredMessageBar();
	[[nodiscard]] bool checkSponsoredMessageBarVisibility() const;
	void createSponsoredMessageBar();
	void setupPinnedTracker();
	void rebuildModeSensitiveBars();
	void checkPinnedBarState();
	void clearHidingPinnedBar();
	void refreshPinnedBarButton(bool many, HistoryItem *item);
	void hidePinnedMessage();
	void resetPinnedState();
	void updateLayout();
	void updateZOrder();
	void applyHeightChange(int was, int now, bool preserveTop);
	void applyHeightChangeWithTopMoved(int was, int now);
	void applyHeightChangeWithRelayout(int was, int now);

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	MsgId _repliesRootId = 0;
	Data::ForumTopic *_topic = nullptr;
	Data::SavedSublist * const _sublist = nullptr;
	PeerId _monoforumPeerId = 0;
	const not_null<Ui::ElasticScroll*> _scroll;
	const not_null<ListWidget*> _list;
	Fn<int()> _keyboardReservedHeight;
	Fn<void(int)> _moveWithTopDelta;
	Fn<void()> _relayout;
	Fn<void(int)> _relayoutWithScrollTopDelta;
	Fn<void()> _showAtStart;
	Fn<void(Data::MessagePosition)> _showAtPosition;
	Fn<ClickHandlerContext(FullMsgId)> _preparePinnedClickContext;

	const std::unique_ptr<Ui::RpWidget> _wrap;
	const std::unique_ptr<Ui::RpWidget> _topBars;
	std::unique_ptr<Ui::GroupCallBar> _groupCallBar;
	std::unique_ptr<Ui::RequestsBar> _requestsBar;
	std::unique_ptr<TranslateBar> _translateBar;
	std::unique_ptr<PinnedTracker> _pinnedTracker;
	std::unique_ptr<Ui::PinnedBar> _pinnedBar;
	std::unique_ptr<Ui::PinnedBar> _hidingPinnedBar;
	base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> _sponsoredMessageBar;
	std::unique_ptr<Ui::PinnedBar> _repliesRootView;
	std::unique_ptr<TopicReopenBar> _topicReopenBar;
	std::unique_ptr<PaysStatus> _paysStatus;
	std::unique_ptr<ContactStatus> _contactStatus;
	std::unique_ptr<BusinessBotStatus> _businessBotStatus;
	rpl::variable<int> _height = 0;
	rpl::variable<bool> _repliesRootVisible = false;
	int _width = 0;
	int _groupCallBarHeight = 0;
	int _requestsBarHeight = 0;
	int _translateBarHeight = 0;
	int _pinnedBarHeight = 0;
	int _sponsoredMessageBarHeight = 0;
	int _repliesRootViewHeight = 0;
	int _topicReopenBarHeight = 0;
	int _paysStatusHeight = 0;
	int _contactStatusHeight = 0;
	int _businessBotStatusHeight = 0;
	HistoryItem *_shownRepliesRootItem = nullptr;
	HistoryItem *_shownPinnedBarItem = nullptr;
	FullMsgId _pinnedClickedId;
	std::optional<FullMsgId> _minPinnedId;
	bool _pinnedBarHasCustomButton = false;
	rpl::lifetime _pinnedLifetime;
	bool _repliesRootViewInited = false;
	bool _repliesRootViewInitScheduled = false;
	bool _modeSensitiveBarsInited = false;
	bool _modeSensitiveShowInForum = false;
	bool _modeSensitiveFullChat = false;
	bool _animatingMode = false;

};

} // namespace HistoryView
