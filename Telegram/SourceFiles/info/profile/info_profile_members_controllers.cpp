/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_members_controllers.h"

#include <rpl/variable.h>
#include "base/weak_ptr.h"
#include "boxes/peers/edit_participants_box.h"
#include "ui/widgets/popup_menu.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "boxes/confirm_box.h"
#include "window/window_controller.h"
#include "styles/style_info.h"
#include "data/data_peer_values.h"

namespace Info {
namespace Profile {

MemberListRow::MemberListRow(
	not_null<UserData*> user,
	Type type)
: PeerListRow(user)
, _type(type) {
}

void MemberListRow::setType(Type type) {
	_type = type;
}

QSize MemberListRow::actionSize() const {
	return canRemove()
		? QRect(
			QPoint(),
			st::infoMembersRemoveIcon.size()).marginsAdded(
				st::infoMembersRemoveIconMargins).size()
		: QSize();
}

void MemberListRow::paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (_type.canRemove && selected) {
		x += st::infoMembersRemoveIconMargins.left();
		y += st::infoMembersRemoveIconMargins.top();
		(actionSelected
			? st::infoMembersRemoveIconOver
			: st::infoMembersRemoveIcon).paint(p, x, y, outerWidth);
	}
}

int MemberListRow::nameIconWidth() const {
	return (_type.rights == Rights::Admin)
		? st::infoMembersAdminIcon.width()
		: (_type.rights == Rights::Creator)
		? st::infoMembersCreatorIcon.width()
		: 0;
}

not_null<UserData*> MemberListRow::user() const {
	return peer()->asUser();
}

void MemberListRow::paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) {
	auto icon = [&] {
		return (_type.rights == Rights::Admin)
			? (selected
				? &st::infoMembersAdminIconOver
				: &st::infoMembersAdminIcon)
			: (selected
				? &st::infoMembersCreatorIconOver
				: &st::infoMembersCreatorIcon);
	}();
	icon->paint(p, x, y, outerWidth);
}

std::unique_ptr<PeerListController> CreateMembersController(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer) {
	return std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		ParticipantsBoxController::Role::Profile);
}

} // namespace Profile
} // namespace Info
