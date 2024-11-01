/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_premium.h"
#include "info/info_content_widget.h"

class UserData;
struct PeerListState;

namespace Info::PeerGifts {

struct ListState {
	std::vector<Api::UserStarGift> list;
	QString offset;
};

class InnerWidget;

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<UserData*> user);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	[[nodiscard]] not_null<UserData*> user() const;

	void setListState(std::unique_ptr<ListState> state);
	std::unique_ptr<ListState> listState();

	~Memento();

private:
	std::unique_ptr<ListState> _listState;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<UserData*> user);

	[[nodiscard]] not_null<UserData*> user() const;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<QString> title() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Info::PeerGifts
