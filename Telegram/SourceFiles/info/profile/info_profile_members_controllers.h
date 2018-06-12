/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Window {
class Navigation;
} // namespace Window

namespace Info {
namespace Profile {

class MemberListRow final : public PeerListRow {
public:
	enum class Rights {
		Normal,
		Admin,
		Creator,
	};
	struct Type {
		Rights rights;
		bool canRemove = false;
	};

	MemberListRow(not_null<UserData*> user, Type type);

	void setType(Type type);
	QSize actionSize() const override;
	void paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;
	int nameIconWidth() const override;
	void paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) override;

	not_null<UserData*> user() const {
		return peer()->asUser();
	}
	bool canRemove() const {
		return _type.canRemove;
	}

private:
	Type _type;

};

std::unique_ptr<PeerListController> CreateMembersController(
	not_null<Window::Navigation*> navigation,
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
