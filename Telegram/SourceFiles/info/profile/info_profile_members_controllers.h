/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Window {
class Controller;
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
	not_null<Window::Controller*> window,
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
