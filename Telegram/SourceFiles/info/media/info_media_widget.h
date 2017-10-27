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
#include "storage/storage_shared_media.h"

namespace Info {
namespace Media {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	using Type = Storage::SharedMediaType;

	Memento(PeerId peerId, Type type)
	: ContentMemento(peerId)
	, _type(type) {
	}

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		rpl::producer<Wrap> wrap,
		not_null<Window::Controller*> controller,
		const QRect &geometry) override;

	Section section() const override {
		return Section(_type);
	}

	Type type() const {
		return _type;
	}

	void setAroundId(FullMsgId aroundId) {
		_aroundId = aroundId;
	}
	FullMsgId aroundId() const {
		return _aroundId;
	}
	void setIdsLimit(int limit) {
		_idsLimit = limit;
	}
	int idsLimit() const {
		return _idsLimit;
	}
	void setScrollTopItem(FullMsgId item) {
		_scrollTopItem = item;
	}
	FullMsgId scrollTopItem() const {
		return _scrollTopItem;
	}
	void setScrollTopShift(int shift) {
		_scrollTopShift = shift;
	}
	int scrollTopShift() const {
		return _scrollTopShift;
	}

private:
	Type _type = Type::Photo;
	FullMsgId _aroundId;
	int _idsLimit = 0;
	FullMsgId _scrollTopItem;
	int _scrollTopShift = 0;;

};

class Widget final : public ContentWidget {
public:
	using Type = Memento::Type;

	Widget(
		QWidget *parent,
		rpl::producer<Wrap> wrap,
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer,
		Type type);

	Type type() const;
	Section section() const override;

	bool showInternal(
		not_null<ContentMemento*> memento) override;
	std::unique_ptr<ContentMemento> createMemento() override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const override;
	void cancelSelection() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);
	
	InnerWidget *_inner = nullptr;

};

} // namespace Media
} // namespace Info
