/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_invite.h"

#include "apiwrap.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "boxes/confirm_box.h"
#include "boxes/abstract_box.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace Api {

void CheckChatInvite(
		not_null<Window::SessionController*> controller,
		const QString &hash,
		ChannelData *invitePeekChannel) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller.get());
	session->api().checkChatInvite(hash, [=](const MTPChatInvite &result) {
		Core::App().hideMediaView();
		result.match([=](const MTPDchatInvite &data) {
			const auto strongController = weak.get();
			if (!strongController) {
				return;
			}
			const auto box = strongController->show(Box<ConfirmInviteBox>(
				session,
				data,
				invitePeekChannel,
				[=] { session->api().importChatInvite(hash); }));
			if (invitePeekChannel) {
				box->boxClosing(
				) | rpl::filter([=] {
					return !invitePeekChannel->amIn();
				}) | rpl::start_with_next([=] {
					if (const auto strong = weak.get()) {
						strong->clearSectionStack(Window::SectionShow(
							Window::SectionShow::Way::ClearStack,
							anim::type::normal,
							anim::activation::background));
					}
				}, box->lifetime());
			}
		}, [=](const MTPDchatInviteAlready &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->clearInvitePeek();
				}
				if (const auto strong = weak.get()) {
					strong->showPeerHistory(
						chat,
						Window::SectionShow::Way::Forward);
				}
			}
		}, [=](const MTPDchatInvitePeek &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->setInvitePeek(hash, data.vexpires().v);
					if (const auto strong = weak.get()) {
						strong->showPeerHistory(
							chat,
							Window::SectionShow::Way::Forward);
					}
				}
			}
		});
	}, [=](const MTP::Error &error) {
		if (error.code() != 400) {
			return;
		}
		Core::App().hideMediaView();
		if (const auto strong = weak.get()) {
			strong->show(
				Box<InformBox>(tr::lng_group_invite_bad_link(tr::now)));
		}
	});
}

} // namespace Api

ConfirmInviteBox::ConfirmInviteBox(
	QWidget*,
	not_null<Main::Session*> session,
	const MTPDchatInvite &data,
	ChannelData *invitePeekChannel,
	Fn<void()> submit)
: _session(session)
, _submit(std::move(submit))
, _title(this, st::confirmInviteTitle)
, _status(this, st::confirmInviteStatus)
, _participants(GetParticipants(_session, data))
, _isChannel(data.is_channel() && !data.is_megagroup()) {
	const auto title = qs(data.vtitle());
	const auto count = data.vparticipants_count().v;
	const auto status = [&] {
		return invitePeekChannel
			? tr::lng_channel_invite_private(tr::now)
			: (!_participants.empty() && _participants.size() < count)
			? tr::lng_group_invite_members(tr::now, lt_count, count)
			: (count > 0)
			? tr::lng_chat_status_members(tr::now, lt_count_decimal, count)
			: _isChannel
			? tr::lng_channel_status(tr::now)
			: tr::lng_group_status(tr::now);
	}();
	_title->setText(title);
	_status->setText(status);

	const auto photo = _session->data().processPhoto(data.vphoto());
	if (!photo->isNull()) {
		_photo = photo->createMediaView();
		_photo->wanted(Data::PhotoSize::Small, Data::FileOrigin());
		if (!_photo->image(Data::PhotoSize::Small)) {
			_session->downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				update();
			}, lifetime());
		}
	} else {
		_photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(0),
			title);
	}
}

ConfirmInviteBox::~ConfirmInviteBox() = default;

auto ConfirmInviteBox::GetParticipants(
	not_null<Main::Session*> session,
	const MTPDchatInvite &data)
-> std::vector<Participant> {
	const auto participants = data.vparticipants();
	if (!participants) {
		return {};
	}
	const auto &v = participants->v;
	auto result = std::vector<Participant>();
	result.reserve(v.size());
	for (const auto &participant : v) {
		if (const auto user = session->data().processUser(participant)) {
			result.push_back(Participant{ user });
		}
	}
	return result;
}

void ConfirmInviteBox::prepare() {
	addButton(
		(_isChannel
			? tr::lng_profile_join_channel()
			: tr::lng_profile_join_group()),
		_submit);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	while (_participants.size() > 4) {
		_participants.pop_back();
	}

	auto newHeight = st::confirmInviteStatusTop + _status->height() + st::boxPadding.bottom();
	if (!_participants.empty()) {
		int skip = (st::boxWideWidth - 4 * st::confirmInviteUserPhotoSize) / 5;
		int padding = skip / 2;
		_userWidth = (st::confirmInviteUserPhotoSize + 2 * padding);
		int sumWidth = _participants.size() * _userWidth;
		int left = (st::boxWideWidth - sumWidth) / 2;
		for (const auto &participant : _participants) {
			auto name = new Ui::FlatLabel(this, st::confirmInviteUserName);
			name->resizeToWidth(st::confirmInviteUserPhotoSize + padding);
			name->setText(participant.user->firstName.isEmpty()
				? participant.user->name
				: participant.user->firstName);
			name->moveToLeft(left + (padding / 2), st::confirmInviteUserNameTop);
			left += _userWidth;
		}

		newHeight += st::confirmInviteUserHeight;
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void ConfirmInviteBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_title->move((width() - _title->width()) / 2, st::confirmInviteTitleTop);
	_status->move((width() - _status->width()) / 2, st::confirmInviteStatusTop);
}

void ConfirmInviteBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_photo) {
		if (const auto image = _photo->image(Data::PhotoSize::Small)) {
			p.drawPixmap(
				(width() - st::confirmInvitePhotoSize) / 2,
				st::confirmInvitePhotoTop,
				image->pixCircled(
					st::confirmInvitePhotoSize,
					st::confirmInvitePhotoSize));
		}
	} else if (_photoEmpty) {
		_photoEmpty->paint(
			p,
			(width() - st::confirmInvitePhotoSize) / 2,
			st::confirmInvitePhotoTop,
			width(),
			st::confirmInvitePhotoSize);
	}

	int sumWidth = _participants.size() * _userWidth;
	int left = (width() - sumWidth) / 2;
	for (auto &participant : _participants) {
		participant.user->paintUserpicLeft(
			p,
			participant.userpic,
			left + (_userWidth - st::confirmInviteUserPhotoSize) / 2,
			st::confirmInviteUserPhotoTop,
			width(),
			st::confirmInviteUserPhotoSize);
		left += _userWidth;
	}
}
