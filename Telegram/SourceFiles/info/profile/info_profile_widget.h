/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
	Memento(PeerId peerId, PeerId migratedPeerId)
	: ContentMemento(peerId, migratedPeerId) {
	}

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
	base::optional<QString> _membersSearch;
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

	std::unique_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Profile
} // namespace Info
