/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Ui {
class Show;
} // namespace Ui

namespace Core {
struct CloudPasswordResult;
} // namespace Core

class PasscodeBox;

class ChannelOwnershipTransfer {
public:
	ChannelOwnershipTransfer(
		not_null<PeerData*> peer,
		not_null<UserData*> selectedUser,
		std::shared_ptr<Ui::Show> show,
		Fn<void(std::shared_ptr<Ui::Show>)> onSuccess = nullptr);

	void start();

private:
	bool handleTransferPasswordError(const QString &error);
	void requestPassword();
	void sendRequest(
		base::weak_qptr<PasscodeBox> box,
		const Core::CloudPasswordResult &result);

	const not_null<PeerData*> _peer;
	const not_null<UserData*> _selectedUser;
	const std::shared_ptr<Ui::Show> _show;
	const Fn<void(std::shared_ptr<Ui::Show>)> _onSuccess;

	rpl::lifetime _lifetime;

};
