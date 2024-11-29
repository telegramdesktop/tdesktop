/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/requests_list/info_requests_list_widget.h"

#include "boxes/peers/edit_peer_requests_box.h"
#include "info/info_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

namespace Info::RequestsList {
namespace {

} // namespace

class InnerWidget final
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
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
	using ListWidget = PeerListContent;

	// PeerListContentDelegate interface
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(object_ptr<Ui::FlatLabel> description) override;
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;

	object_ptr<ListWidget> setupList(
		RpWidget *parent,
		not_null<RequestsBoxController*> controller);

	const std::shared_ptr<Main::SessionShow> _show;
	not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;
	std::unique_ptr<RequestsBoxController> _listController;
	object_ptr<ListWidget> _list;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _show(controller->uiShow())
, _controller(controller)
, _peer(peer)
, _listController(std::make_unique<RequestsBoxController>(
	controller,
	_peer))
, _list(setupList(this, _listController.get())) {
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	controller->searchFieldController()->queryValue(
	) | rpl::start_with_next([this](QString &&query) {
		peerListScrollToTop();
		content()->searchQueryChanged(std::move(query));
	}, lifetime());
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setListState(_listController->saveState());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_listController->restoreState(memento->listState());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	desired += _list->fullRowsCount() * st::infoMembersList.item.height;
	return qMax(height(), desired);
}

object_ptr<InnerWidget::ListWidget> InnerWidget::setupList(
		RpWidget *parent,
		not_null<RequestsBoxController*> controller) {
	auto result = object_ptr<ListWidget>(parent, controller);
	result->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		auto addmin = (request.ymin < 0) ? 0 : st::infoCommonGroupsMargin.top();
		auto addmax = (request.ymax < 0) ? 0 : st::infoCommonGroupsMargin.top();
		_scrollToRequests.fire({
			request.ymin + addmin,
			request.ymax + addmax });
	}, result->lifetime());
	result->moveToLeft(0, st::infoCommonGroupsMargin.top());

	parent->widthValue(
	) | rpl::start_with_next([list = result.data()](int newWidth) {
		list->resizeToWidth(newWidth);
	}, result->lifetime());

	result->heightValue(
	) | rpl::start_with_next([parent](int listHeight) {
		auto newHeight = st::infoCommonGroupsMargin.top()
			+ listHeight
			+ st::infoCommonGroupsMargin.bottom();
		parent->resize(parent->width(), newHeight);
	}, result->lifetime());

	return result;
}

void InnerWidget::peerListSetTitle(rpl::producer<QString> title) {
}

void InnerWidget::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool InnerWidget::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int InnerWidget::peerListSelectedRowsCount() {
	return 0;
}

void InnerWidget::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void InnerWidget::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListFinishSelectedRowsBunch() {
}

void InnerWidget::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

std::shared_ptr<Main::SessionShow> InnerWidget::peerListUiShow() {
	return _show;
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(peer, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::RequestsList);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, peer());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<PeerListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<PeerListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: ContentWidget(parent, controller) {
	controller->setSearchEnabledByContent(true);
	_inner = setInnerWidget(object_ptr<InnerWidget>(this, controller, peer));
}

rpl::producer<QString> Widget::title() {
	return tr::lng_manage_peer_requests();
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto requestsMemento = dynamic_cast<Memento*>(memento.get())) {
		if (requestsMemento->peer() == peer()) {
			restoreState(requestsMemento);
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
	auto result = std::make_shared<Memento>(peer());
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

} // namespace Info::RequestsList
