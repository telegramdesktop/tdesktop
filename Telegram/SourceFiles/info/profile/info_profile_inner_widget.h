/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

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

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<RpWidget> setupContent(
		not_null<RpWidget*> parent,
		Origin origin);
	object_ptr<RpWidget> setupSharedMedia(
		not_null<RpWidget*> parent,
		rpl::producer<bool> showDivider,
		Ui::MultiSlideTracker &sharedTracker);
	void setupMembers(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> showDivider);
	void setupSavedMusic(not_null<Ui::VerticalLayout*> container);

	int countDesiredHeight() const;
	void updateDesiredHeight() {
		_desiredHeight.fire(countDesiredHeight());
	}

	void addAboutVerificationOrDivider(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<bool> showDivider);

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;
	PeerData * const _migrated = nullptr;
	Data::ForumTopic * const _topic = nullptr;
	Data::SavedSublist * const _sublist = nullptr;

	bool _inResize = false;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<int> _desiredHeight;

	rpl::variable<bool> _backToggles;
	rpl::event_stream<> _backClicks;
	rpl::event_stream<int> _onlineCount;
	rpl::event_stream<> _showFinished;

	PeerData *_reactionGroup = nullptr;

	std::shared_ptr<Data::PhotoMedia> _nonPersonalView;

	rpl::variable<std::optional<QColor>> _topBarColor;

	Members *_members = nullptr;
	Ui::SlideWrap<RpWidget> *_sharedMediaWrap = nullptr;
	object_ptr<RpWidget> _content;

	bool _aboutVerificationAdded = false;

};

} // namespace Profile
} // namespace Info
