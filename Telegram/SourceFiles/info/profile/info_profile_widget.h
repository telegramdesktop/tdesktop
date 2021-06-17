/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include "info/info_content_widget.h"

namespace Info {
namespace Profile {

class InnerWidget;
struct MembersState;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer, PeerId migratedPeerId);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	void setInfoExpanded(bool expanded) {
		_infoExpanded = expanded;
	}
	bool infoExpanded() const {
		return _infoExpanded;
	}
	void setMembersState(std::unique_ptr<MembersState> state);
	std::unique_ptr<MembersState> membersState();

	~Memento();

private:
	bool _infoExpanded = true;
	std::unique_ptr<MembersState> _membersState;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller);

	void setIsStackBottom(bool isStackBottom) override;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	void setInnerFocus() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Profile
} // namespace Info
