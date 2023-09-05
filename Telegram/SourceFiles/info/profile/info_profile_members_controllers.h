/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/peer_list_controllers.h"
#include "ui/unread_badge.h"

class ParticipantsBoxController;

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Info {
namespace Profile {

class MemberListRow final : public PeerListRowWithLink {
public:
	enum class Rights {
		Normal,
		Admin,
		Creator,
	};
	struct Type {
		Rights rights;
		QString adminRank;
	};

	MemberListRow(not_null<UserData*> user, Type type);

	void setType(Type type);
	bool rightActionDisabled() const override;
	QMargins rightActionMargins() const override;
	void refreshStatus() override;

	not_null<UserData*> user() const;

private:
	Type _type;

};

std::unique_ptr<ParticipantsBoxController> CreateMembersController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
