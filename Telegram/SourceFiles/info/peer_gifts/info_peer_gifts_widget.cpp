/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_widget.h"

#include "data/data_user.h"
#include "info/info_controller.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"

namespace Info::PeerGifts {
namespace {

} // namespace

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<UserData*> user);

	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
	}

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	int desiredHeight() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	const std::shared_ptr<Main::SessionShow> _show;
	not_null<Controller*> _controller;
	const not_null<UserData*> _user;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: RpWidget(parent)
, _show(controller->uiShow())
, _controller(controller)
, _user(user) {
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	auto state = std::make_unique<ListState>();
	memento->setListState(std::move(state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (const auto state = memento->listState()) {

	}
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;

	return qMax(height(), desired);
}

Memento::Memento(not_null<UserData*> user)
: ContentMemento(user, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::PeerGifts);
}

not_null<UserData*> Memento::user() const {
	return peer()->asUser();
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, user());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<ListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<ListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller,
		user));
}

rpl::producer<QString> Widget::title() {
	return tr::lng_peer_gifts_title();
}

not_null<UserData*> Widget::user() const {
	return _inner->user();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto similarMemento = dynamic_cast<Memento*>(memento.get())) {
		if (similarMemento->user() == user()) {
			restoreState(similarMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(user());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::PeerGifts
