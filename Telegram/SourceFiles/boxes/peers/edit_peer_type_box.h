/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"

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
		FnMut<void(Privacy, QString)> savedCallback,
		std::optional<Privacy> privacySaved = std::nullopt,
		std::optional<QString> usernameSaved = std::nullopt);

protected:
	void prepare() override;

private:
	void setupContent();

	not_null<PeerData*> _peer;
	FnMut<void(Privacy, QString)> _savedCallback;

	rpl::event_stream<> _focusRequests;

};
