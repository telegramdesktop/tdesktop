/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_channel_earn.h"
#include "info/info_content_widget.h"

namespace Info::ChannelEarn {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer);
	~Memento();

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	using SavedState = Data::EarnStatistics;

	void setState(SavedState states);
	[[nodiscard]] SavedState state();

private:
	SavedState _state;

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller);

	bool showInternal(not_null<ContentMemento*> memento) override;
	rpl::producer<QString> title() override;
	rpl::producer<bool> desiredShadowVisibility() const override;
	void showFinished() override;
	void setInnerFocus() override;

	[[nodiscard]] not_null<PeerData*> peer() const;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	const not_null<InnerWidget*> _inner;

};

[[nodiscard]] std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer);

} // namespace Info::ChannelEarn
