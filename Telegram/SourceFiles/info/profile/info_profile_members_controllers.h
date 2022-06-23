/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_controllers.h"

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Info {
namespace Profile {

enum class Badge;

class MemberListRow final : public PeerListRowWithLink {
public:
	enum class Rights {
		Normal,
		Admin,
		Creator,
	};
	struct Type {
		Badge badge;
		Rights rights;
		QString adminRank;
	};

	MemberListRow(not_null<UserData*> user, Type type);

	void setType(Type type);
	bool rightActionDisabled() const override;
	QMargins rightActionMargins() const override;
	int nameIconWidth() const override;
	void paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) override;
	void refreshStatus() override;

	not_null<UserData*> user() const;

private:
	Type _type;
	QSize _fakeScamSize;

};

std::unique_ptr<PeerListController> CreateMembersController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
