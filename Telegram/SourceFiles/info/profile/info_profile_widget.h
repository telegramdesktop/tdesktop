/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "ui/effects/animations.h"

namespace Data {
class ForumTopic;
} // namespace Data

namespace Info::Profile {

class InnerWidget;
class TabsHost;
struct MembersState;

struct GroupReactionOrigin {
	not_null<PeerData*> group;
	MsgId messageId = 0;
};

struct Origin {
	std::variant<v::null_t, GroupReactionOrigin> data;
};

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<Controller*> controller);
	Memento(
		not_null<PeerData*> peer,
		PeerId migratedPeerId,
		Origin origin = { v::null });
	explicit Memento(not_null<Data::ForumTopic*> topic);
	explicit Memento(not_null<Data::SavedSublist*> sublist);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Info::Section section() const override;

	[[nodiscard]] Origin origin() const {
		return _origin;
	}

	void setMembersState(std::unique_ptr<MembersState> state);
	[[nodiscard]] std::unique_ptr<MembersState> membersState();

	void setActiveTab(const QString &id) {
		_activeTab = id;
	}
	[[nodiscard]] QString activeTab() const {
		return _activeTab;
	}

	~Memento();

private:
	Memento(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Data::SavedSublist *sublist,
		PeerId migratedPeerId,
		Origin origin);

	std::unique_ptr<MembersState> _membersState;
	Origin _origin;
	QString _activeTab;

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller, Origin origin);

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	void setInnerFocus() override;
	void enableBackButton() override;
	void showFinished() override;

	rpl::producer<QString> title() override;
	rpl::producer<Dialogs::Stories::Content> titleStories() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);
	void setupTabsStripFloat();
	void updateTabsStripFloatGeometry();
	[[nodiscard]] auto swipeTabsFinishData(
		Ui::Controls::SwipeHandlerInitData data)
	-> Ui::Controls::SwipeHandlerFinishData;

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	FlexibleScrollData _flexibleScroll;
	InnerWidget *_inner = nullptr;
	base::weak_qptr<Ui::RpWidget> _pinnedToTop;
	base::weak_qptr<Ui::RpWidget> _pinnedToBottom;
	std::unique_ptr<FlexibleScrollHelper> _flexibleScrollHelper;
	base::unique_qptr<Ui::RpWidget> _tabsStripFloat;
	base::weak_qptr<TabsHost> _tabsHost;
	base::weak_qptr<Ui::RpWidget> _tabsStrip;

};

} // namespace Info::Profile
