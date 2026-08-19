/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "info/profile/info_profile_section_stack.h"
#include "ui/rp_widget.h"

namespace Data {
class ForumTopic;
class SavedSublist;
class PhotoMedia;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class VerticalLayout;
template <typename Widget>
class SlideWrap;
struct ScrollToRequest;
class MultiSlideTracker;
} // namespace Ui

namespace Info {

enum class Wrap;
class Controller;

namespace Profile {

class Memento;
class Members;
class TabsHost;
class TopBar;
struct Origin;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		Origin origin);

	[[nodiscard]] rpl::producer<> backRequest() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<int> desiredHeightValue() const override;

	bool hasFlexibleTopBar() const;
	base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<Ui::RpWidget*> parent);
	base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent);

	void enableBackButton();
	void showFinished();
	void checkBeforeCloseByEscape(Fn<void()> close);
	[[nodiscard]] bool searchAvailable() const;
	void showSearch();

	[[nodiscard]] TabsHost *tabsHost() const {
		return _tabsHost;
	}
	[[nodiscard]] rpl::producer<bool> tabsDockedValue() const {
		return _tabsDocked.value();
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<RpWidget> setupContent(
		not_null<RpWidget*> parent,
		Origin origin);
	[[nodiscard]] Section makeMembersSection(
		not_null<QWidget*> parent,
		rpl::producer<bool> shown);

	int countDesiredHeight() const;
	void updateDesiredHeight() {
		const auto value = countDesiredHeight();
		if (_lastDesiredHeight != value) {
			_lastDesiredHeight = value;
			_desiredHeight = value;
		}
	}

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;
	PeerData * const _migrated = nullptr;
	Data::ForumTopic * const _topic = nullptr;
	Data::SavedSublist * const _sublist = nullptr;

	bool _inResize = false;
	int _lastDesiredHeight = -1;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::variable<int> _desiredHeight = 0;

	rpl::variable<bool> _backToggles;
	rpl::event_stream<> _backClicks;
	rpl::event_stream<int> _onlineCount;
	rpl::event_stream<> _showFinished;

	std::shared_ptr<Data::PhotoMedia> _nonPersonalView;

	rpl::variable<std::optional<QColor>> _topBarColor;

	Members *_members = nullptr;
	base::weak_qptr<TopBar> _topBar;
	Ui::SlideWrap<RpWidget> *_sharedMediaWrap = nullptr;
	TabsHost *_tabsHost = nullptr;
	rpl::variable<bool> _tabsDocked = false;
	bool _clampingTabsScroll = false;
	object_ptr<RpWidget> _content;

};

} // namespace Profile
} // namespace Info
