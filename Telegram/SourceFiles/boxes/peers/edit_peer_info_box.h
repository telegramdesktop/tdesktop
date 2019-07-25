/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "boxes/abstract_box.h"

namespace style {
struct InfoProfileCountButton;
} // namespace style

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

class EditPeerInfoBox : public BoxContent {
public:
	EditPeerInfoBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);

	void setInnerFocus() override {
		_focusRequests.fire({});
	}

	static bool Available(not_null<PeerData*> peer);

	[[nodiscard]] static object_ptr<Info::Profile::Button> CreateButton(
		not_null<QWidget*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::InfoProfileCountButton &st,
		const style::icon *icon = nullptr);

protected:
	void prepare() override;

private:
	rpl::event_stream<> _focusRequests;
	not_null<Window::SessionNavigation*> _navigation;
	not_null<PeerData*> _peer;

};
