/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Info::Profile {

enum class OwnQrLinkKind {
	Username,
	Phone,
	Contact,
};

struct OwnQrLink {
	OwnQrLinkKind kind = OwnQrLinkKind::Contact;
	QString link;
	QString label = u"0:00"_q;
	TimeId expires = 0;

	friend inline bool operator==(const OwnQrLink &, const OwnQrLink &) = default;
};

[[nodiscard]] rpl::producer<OwnQrLink> OwnQrLinkValue(
	not_null<Main::Session*> session,
	Fn<void(QString)> fail);

} // namespace Info::Profile
