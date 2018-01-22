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
namespace FeedProfile {

class InnerWidget;
struct ChannelsState;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<Data::Feed*> feed);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	void setChannelsState(std::unique_ptr<ChannelsState> state);
	std::unique_ptr<ChannelsState> channelsState();

	~Memento();

private:
	std::unique_ptr<ChannelsState> _channelsState;

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

} // namespace FeedProfile
} // namespace Info
