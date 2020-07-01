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

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class CloudImageView;
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
	struct Participant {
		not_null<UserData*> user;
		std::shared_ptr<Data::CloudImageView> userpic;
	};
	static std::vector<Participant> GetParticipants(
		not_null<Main::Session*> session,
		const MTPDchatInvite &data);

	const not_null<Main::Session*> _session;

	Fn<void()> _submit;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _status;
	std::shared_ptr<Data::PhotoMedia> _photo;
	std::unique_ptr<Ui::EmptyUserpic> _photoEmpty;
	std::vector<Participant> _participants;
	bool _isChannel = false;

	int _userWidth = 0;

};
