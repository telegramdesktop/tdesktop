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

#include <rpl/producer.h>
#include <rpl/map.h>
#include "observer_peer.h"

namespace Ui {
class RpWidget;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
namespace Profile {

inline auto WithEmptyEntities() {
	return rpl::map([](QString &&text) {
		return TextWithEntities{ std::move(text), {} };
	});
}

inline auto ToUpperValue() {
	return rpl::map([](QString &&text) {
		return std::move(text).toUpper();
	});
}

rpl::producer<Notify::PeerUpdate> PeerUpdateValue(
	not_null<PeerData*> peer,
	Notify::PeerUpdate::Flags flags);

rpl::producer<TextWithEntities> PhoneValue(
	not_null<UserData*> user);
rpl::producer<TextWithEntities> BioValue(
	not_null<UserData*> user);
rpl::producer<TextWithEntities> UsernameValue(
	not_null<UserData*> user);
rpl::producer<TextWithEntities> AboutValue(
	not_null<PeerData*> peer);
rpl::producer<TextWithEntities> LinkValue(
	not_null<PeerData*> peer);
rpl::producer<bool> NotificationsEnabledValue(
	not_null<PeerData*> peer);
rpl::producer<bool> IsContactValue(
	not_null<UserData*> user);
rpl::producer<bool> CanShareContactValue(
	not_null<UserData*> user);
rpl::producer<bool> CanAddContactValue(
	not_null<UserData*> user);
rpl::producer<int> MembersCountValue(
	not_null<PeerData*> peer);
rpl::producer<int> SharedMediaCountValue(
	not_null<PeerData*> peer,
	Storage::SharedMediaType type);
rpl::producer<int> CommonGroupsCountValue(
	not_null<UserData*> user);
rpl::producer<bool> CanAddMemberValue(
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
