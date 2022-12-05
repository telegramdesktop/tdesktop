/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

class UserData;
class ChannelData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Ui {
class EmptyUserpic;
} // namespace Ui

namespace Api {

void CheckChatInvite(
	not_null<Window::SessionController*> controller,
	const QString &hash,
	ChannelData *invitePeekChannel = nullptr);

} // namespace Api

class ConfirmInviteBox final : public Ui::BoxContent {
public:
	ConfirmInviteBox(
		QWidget*,
		not_null<Main::Session*> session,
		const MTPDchatInvite &data,
		ChannelData *invitePeekChannel,
		Fn<void()> submit);
	~ConfirmInviteBox();

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Participant;
	struct ChatInvite {
		QString title;
		QString about;
		PhotoData *photo = nullptr;
		int participantsCount = 0;
		std::vector<Participant> participants;
		bool isPublic = false;
		bool isChannel = false;
		bool isMegagroup = false;
		bool isBroadcast = false;
		bool isRequestNeeded = false;
	};
	[[nodiscard]] static ChatInvite Parse(
		not_null<Main::Session*> session,
		const MTPDchatInvite &data);

	ConfirmInviteBox(
		not_null<Main::Session*> session,
		ChatInvite &&invite,
		ChannelData *invitePeekChannel,
		Fn<void()> submit);

	const not_null<Main::Session*> _session;

	Fn<void()> _submit;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _about;
	object_ptr<Ui::FlatLabel> _aboutRequests;
	std::shared_ptr<Data::PhotoMedia> _photo;
	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;
	std::vector<Participant> _participants;
	bool _isChannel = false;
	bool _requestApprove = false;

	int _userWidth = 0;

};
