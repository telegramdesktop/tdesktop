/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

class ChannelData;
struct PeerListState;

namespace Api {
struct WhoReadList;
} // namespace Api

namespace Info::ReactionsList {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	[[nodiscard]] std::shared_ptr<Api::WhoReadList> whoReadIds() const;
	[[nodiscard]] FullMsgId contextId() const;
	[[nodiscard]] Data::ReactionId selected() const;

	void setListState(std::unique_ptr<PeerListState> state);
	std::unique_ptr<PeerListState> listState();

	~Memento();

private:
	std::unique_ptr<PeerListState> _listState;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller,
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected);

	[[nodiscard]] std::shared_ptr<Api::WhoReadList> whoReadIds() const;
	[[nodiscard]] FullMsgId contextId() const;
	[[nodiscard]] Data::ReactionId selected() const;

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

} // namespace Info::ReactionsList
