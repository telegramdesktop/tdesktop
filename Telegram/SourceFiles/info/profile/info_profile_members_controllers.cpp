/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_members_controllers.h"

#include "boxes/peers/edit_participants_box.h"
#include "info/profile/info_profile_values.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "ui/unread_badge.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"

namespace Info {
namespace Profile {

MemberListRow::MemberListRow(
	not_null<UserData*> user,
	Type type)
: PeerListRowWithLink(user)
, _type(type) {
	setType(type);
}

void MemberListRow::setType(Type type) {
	_type = type;
	_fakeScamSize = (_type.badge == Badge::Fake)
		? Ui::ScamBadgeSize(true)
		: (_type.badge == Badge::Scam)
		? Ui::ScamBadgeSize(false)
		: QSize();
	PeerListRowWithLink::setActionLink(!_type.adminRank.isEmpty()
		? _type.adminRank
		: (_type.rights == Rights::Creator)
		? tr::lng_owner_badge(tr::now)
		: (_type.rights == Rights::Admin)
		? tr::lng_admin_badge(tr::now)
		: QString());
}

bool MemberListRow::rightActionDisabled() const {
	return true;
}

QMargins MemberListRow::rightActionMargins() const {
	const auto skip = st::contactsCheckPosition.x();
	return QMargins(
		skip,
		st::defaultPeerListItem.namePosition.y(),
		st::defaultPeerListItem.photoPosition.x() + skip,
		0);
}

int MemberListRow::nameIconWidth() const {
	switch (_type.badge) {
	case Badge::None: return 0;
	case Badge::Verified: return st::dialogsVerifiedIcon.width();
	case Badge::Premium: return st::dialogsPremiumIcon.width();
	case Badge::Scam:
	case Badge::Fake:
		return st::dialogsScamSkip + _fakeScamSize.width();
	}
	return 0;
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
	switch (_type.badge) {
	case Badge::None: return;
	case Badge::Verified:
		(selected
			? st::dialogsVerifiedIconOver
			: st::dialogsVerifiedIcon).paint(p, x, y, outerWidth);
		break;
	case Badge::Premium:
		(selected
			? st::dialogsPremiumIconOver
			: st::dialogsPremiumIcon).paint(p, x, y, outerWidth);
		break;
	case Badge::Scam:
	case Badge::Fake:
		return Ui::DrawScamBadge(
			(_type.badge == Badge::Fake),
			p,
			QRect(
				x + st::dialogsScamSkip,
				y + (st::normalFont->height - _fakeScamSize.height()) / 2,
				_fakeScamSize.width(),
				_fakeScamSize.height()),
			outerWidth,
			(selected ? st::dialogsScamFgOver : st::dialogsScamFg));
	}
}

void MemberListRow::refreshStatus() {
	if (user()->isBot()) {
		const auto seesAllMessages = (user()->botInfo->readsAllHistory
			|| _type.rights != Rights::Normal);
		setCustomStatus(seesAllMessages
			? tr::lng_status_bot_reads_all(tr::now)
			: tr::lng_status_bot_not_reads_all(tr::now));
	} else {
		PeerListRow::refreshStatus();
	}
}

std::unique_ptr<PeerListController> CreateMembersController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	return std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		ParticipantsBoxController::Role::Profile);
}

} // namespace Profile
} // namespace Info
