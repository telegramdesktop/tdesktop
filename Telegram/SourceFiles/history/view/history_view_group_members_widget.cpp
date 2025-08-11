/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_group_members_widget.h"

#include "boxes/peers/edit_participants_box.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class GroupMembersWidgetController : public ParticipantsBoxController {
public:
	using ParticipantsBoxController::ParticipantsBoxController;

protected:
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override {
		return nullptr;
	}

};

} // namespace

GroupMembersWidget::GroupMembersWidget(
not_null<Ui::RpWidget*> parent,
not_null<Window::SessionNavigation*> navigation,
not_null<PeerData*> peer)
: Ui::RpWidget(parent)
, _show(navigation->uiShow())
, _listController(std::make_unique<GroupMembersWidgetController>(
		navigation,
		peer,
		ParticipantsBoxController::Role::Profile)) {
	_listController->setStoriesShown(true);
	setupList();
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
}

void GroupMembersWidget::setupList() {
	const auto topSkip = 0;
	_listController->setStyleOverrides(&st::groupMembersWidgetList);
	_listController->setStoriesShown(true);
	_list = object_ptr<PeerListContent>(this, _listController.get());
	widthValue() | rpl::start_with_next([this](int newWidth) {
		if (newWidth > 0) {
			_list->resizeToWidth(newWidth);
		}
	}, _list->lifetime());
	_list->heightValue() | rpl::start_with_next([=](int listHeight) {
		if (const auto newHeight = topSkip + listHeight; newHeight > 0) {
			resize(width(), newHeight);
		}
	}, _list->lifetime());
	_list->moveToLeft(0, topSkip);
}

void GroupMembersWidget::peerListSetTitle(rpl::producer<QString> title) {
}

void GroupMembersWidget::peerListSetAdditionalTitle(
	rpl::producer<QString> title) {
}

bool GroupMembersWidget::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int GroupMembersWidget::peerListSelectedRowsCount() {
	return 0;
}

void GroupMembersWidget::peerListScrollToTop() {
}

void GroupMembersWidget::peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void GroupMembersWidget::peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void GroupMembersWidget::peerListFinishSelectedRowsBunch() {
}

std::shared_ptr<Main::SessionShow> GroupMembersWidget::peerListUiShow() {
	return _show;
}

void GroupMembersWidget::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

void GroupMembersWidget::peerListShowRowMenu(
	not_null<PeerListRow*> row,
	bool highlightRow,
	Fn<void(not_null<Ui::PopupMenu*>)> destroyed) {
}

} // namespace HistoryView
