/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

class PeerData;
struct PeerListState;

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::SimilarPeers {

class InnerWidget;

[[nodiscard]] object_ptr<Ui::RpWidget> MakeSimilarPeersInner(
	QWidget *parent,
	not_null<AbstractController*> controller,
	not_null<PeerData*> peer);

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<PeerData*> peer);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

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
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const;

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

} // namespace Info::SimilarPeers
