/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

class ChannelData;
class UserData;

namespace Ui {
class PopupMenu;
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace AntiSpamMenu {

class AntiSpamValidator final {
public:
	AntiSpamValidator(
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel);

	[[nodiscard]] object_ptr<Ui::RpWidget> createButton() const;

	void resolveUser(Fn<void()> finish) const;
	[[nodiscard]] UserData *maybeAppendUser() const;
	[[nodiscard]] UserId userId() const;
	void addAction(not_null<Ui::PopupMenu*> menu, FullMsgId fakeId) const;
	void addEventMsgId(FullMsgId fakeId, MsgId realId);

private:
	const not_null<ChannelData*> _channel;
	const not_null<Window::SessionController*> _controller;

	base::flat_map<FullMsgId, MsgId> _itemEventMsgIds;

};

} // namespace AntiSpamMenu
