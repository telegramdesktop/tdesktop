/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

class PeerData;
class PeerListController;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Info::CommunityRequests {

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
	~Widget();

	[[nodiscard]] not_null<PeerData*> peer() const;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<QString> title() override;

	void showFinished() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	std::unique_ptr<Ui::RpWidget> setupBottomBar();
	void updateBottomBarGeometry();
	void processAll(bool reject);

	std::unique_ptr<PeerListController> _listController;
	InnerWidget *_inner = nullptr;
	std::unique_ptr<Ui::RpWidget> _bottom;

};

} // namespace Info::CommunityRequests
