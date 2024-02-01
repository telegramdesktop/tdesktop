/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

namespace Data {
class ForumTopic;
} // namespace Data

namespace Info::Profile {

class InnerWidget;
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

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	[[nodiscard]] Origin origin() const {
		return _origin;
	}

	void setMembersState(std::unique_ptr<MembersState> state);
	std::unique_ptr<MembersState> membersState();

	~Memento();

private:
	Memento(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		PeerId migratedPeerId,
		Origin origin);

	std::unique_ptr<MembersState> _membersState;
	Origin _origin;

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

	rpl::producer<QString> title() override;
	rpl::producer<Dialogs::Stories::Content> titleStories() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Info::Profile
