/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

struct PeerListState;

namespace Data {
class Feed;
} // namespace Data

namespace Info {
namespace FeedProfile {
class Channels;
struct ChannelsState;
} // namespace FeedProfile

namespace Channels {

using SavedState = FeedProfile::ChannelsState;

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<Controller*> controller);
	explicit Memento(not_null<Data::Feed*> feed);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	void setState(std::unique_ptr<SavedState> state);
	std::unique_ptr<SavedState> state();

	~Memento();

private:
	std::unique_ptr<SavedState> _state;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller);

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::unique_ptr<ContentMemento> doCreateMemento() override;

	FeedProfile::Channels *_inner = nullptr;

};

} // namespace Channels
} // namespace Info
