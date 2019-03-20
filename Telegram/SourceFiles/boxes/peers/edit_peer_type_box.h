/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"

enum LangKey : int;

namespace style {
struct InfoProfileCountButton;
} // namespace style

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

enum class Privacy {
	Public,
	Private,
};

enum class UsernameState {
	Normal,
	TooMany,
	NotAvailable,
};

class EditPeerTypeBox : public BoxContent {
public:
	EditPeerTypeBox(
		QWidget*,
		not_null<PeerData*> p,
		std::optional<FnMut<void(Privacy, QString)>> savedCallback = {},
		std::optional<Privacy> privacySaved = {},
		std::optional<QString> usernameSaved = {},
		std::optional<LangKey> usernameError = {});

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	not_null<PeerData*> _peer;
	std::optional<FnMut<void(Privacy, QString)>> _savedCallback;

	std::optional<Privacy> _privacySavedValue;
	std::optional<QString> _usernameSavedValue;
	std::optional<LangKey> _usernameError;

	rpl::event_stream<> _focusRequests;

};
