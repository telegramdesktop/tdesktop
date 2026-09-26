/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

class PeerData;
class ChannelData;

namespace Info::Community {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<PeerData*> peer);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	~Memento();

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	void enableBackButton() override;
	void showFinished() override;

	rpl::producer<QString> title() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	std::unique_ptr<Ui::RpWidget> setupAddChatButton();

	FlexibleScrollData _flexibleScroll;
	InnerWidget *_inner = nullptr;
	base::weak_qptr<Ui::RpWidget> _pinnedToTop;
	std::unique_ptr<FlexibleScrollHelper> _flexibleScrollHelper;
	std::unique_ptr<Ui::RpWidget> _bottom;

};

} // namespace Info::Community
