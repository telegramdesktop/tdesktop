/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/common_groups/info_common_groups_inner_widget.h"

#include "info/common_groups/info_common_groups_widget.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "mtproto/sender.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"

namespace Info {
namespace CommonGroups {
namespace {

constexpr auto kCommonGroupsPerPage = 40;
constexpr auto kCommonGroupsSearchAfter = 20;

class ListController : public PeerListController , private base::Subscriber {
public:
	ListController(
		not_null<Controller*> controller,
		not_null<UserData*> user);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createRestoredRow(
			not_null<PeerData*> peer) override {
		return createRow(peer);
	}

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer);

	struct SavedState : SavedStateBase {
		PeerId preloadGroupId = 0;
		bool allLoaded = false;
		bool wasLoading = false;
	};
	const not_null<Controller*> _controller;
	MTP::Sender _api;
	not_null<UserData*> _user;
	mtpRequestId _preloadRequestId = 0;
	bool _allLoaded = false;
	PeerId _preloadGroupId = 0;

};

ListController::ListController(
	not_null<Controller*> controller,
	not_null<UserData*> user)
: PeerListController()
, _controller(controller)
, _api(&_controller->session().mtp())
, _user(user) {
	_controller->setSearchEnabledByContent(false);
}

Main::Session &ListController::session() const {
	return _user->session();
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	result->setCustomStatus(QString());
	return result;
}

void ListController::prepare() {
	setSearchNoResultsText(tr::lng_bot_groups_not_found(tr::now));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(tr::lng_profile_common_groups_section());
}

void ListController::loadMoreRows() {
	if (_preloadRequestId || _allLoaded) {
		return;
	}
	_preloadRequestId = _api.request(MTPmessages_GetCommonChats(
		_user->inputUser,
		MTP_int(peerIsChat(_preloadGroupId)
			? peerToChat(_preloadGroupId).bare
			: peerToChannel(_preloadGroupId).bare), // #TODO ids
		MTP_int(kCommonGroupsPerPage)
	)).done([this](const MTPmessages_Chats &result) {
		_preloadRequestId = 0;
		_preloadGroupId = 0;
		_allLoaded = true;
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		if (!chats.empty()) {
			for (const auto &chat : chats) {
				if (const auto peer = _user->owner().processChat(chat)) {
					if (!peer->migrateTo()) {
						delegate()->peerListAppendRow(
							createRow(peer));
					}
					_preloadGroupId = peer->id;
					_allLoaded = false;
				}
			}
			delegate()->peerListRefreshRows();
		}
		auto fullCount = delegate()->peerListFullRowsCount();
		if (fullCount > kCommonGroupsSearchAfter) {
			_controller->setSearchEnabledByContent(true);
		}
	}).send();
}

std::unique_ptr<PeerListState> ListController::saveState() const {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	my->preloadGroupId = _preloadGroupId;
	my->allLoaded = _allLoaded;
	my->wasLoading = (_preloadRequestId != 0);
	result->controllerState = std::move(my);
	return result;
}

void ListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (auto my = dynamic_cast<SavedState*>(typeErasedState)) {
		if (auto requestId = base::take(_preloadRequestId)) {
			_api.request(requestId).cancel();
		}
		_allLoaded = my->allLoaded;
		_preloadGroupId = my->preloadGroupId;
		if (my->wasLoading) {
			loadMoreRows();
		}
		PeerListController::restoreState(std::move(state));
		auto fullCount = delegate()->peerListFullRowsCount();
		if (fullCount > kCommonGroupsSearchAfter) {
			_controller->setSearchEnabledByContent(true);
		}
	}
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_controller->parentController()->showPeerHistory(
		row->peer(),
		Window::SectionShow::Way::Forward);
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: RpWidget(parent)
, _controller(controller)
, _user(user)
, _listController(std::make_unique<ListController>(controller, _user))
, _list(setupList(this, _listController.get())) {
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	_controller->searchFieldController()->queryValue(
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
	auto count = qMax(_user->commonChatsCount(), 1);
	desired += qMax(count, _list->fullRowsCount())
		* st::infoCommonGroupsList.item.height;
	return qMax(height(), desired);
}

object_ptr<InnerWidget::ListWidget> InnerWidget::setupList(
		RpWidget *parent,
		not_null<PeerListController*> controller) const {
	controller->setStyleOverrides(&st::infoCommonGroupsList);
	auto result = object_ptr<ListWidget>(
		parent,
		controller);
	result->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		auto addmin = (request.ymin < 0)
			? 0
			: st::infoCommonGroupsMargin.top();
		auto addmax = (request.ymax < 0)
			? 0
			: st::infoCommonGroupsMargin.top();
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

} // namespace CommonGroups
} // namespace Info

