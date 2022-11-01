/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_invite.h"

#include "apiwrap.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toasts/common_toasts.h"
#include "boxes/premium_limits_box.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace Api {

namespace {

void SubmitChatInvite(
		base::weak_ptr<Window::SessionController> weak,
		not_null<Main::Session*> session,
		const QString &hash,
		bool isGroup) {
	session->api().request(MTPmessages_ImportChatInvite(
		MTP_string(hash)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		const auto strongController = weak.get();
		if (!strongController) {
			return;
		}

		strongController->hideLayer();
		const auto handleChats = [&](const MTPVector<MTPChat> &chats) {
			if (chats.v.isEmpty()) {
				return;
			}
			const auto peerId = chats.v[0].match([](const MTPDchat &data) {
				return peerFromChat(data.vid().v);
			}, [](const MTPDchannel &data) {
				return peerFromChannel(data.vid().v);
			}, [](auto&&) {
				return PeerId(0);
			});
			if (const auto peer = session->data().peerLoaded(peerId)) {
				// Shows in the primary window anyway.
				strongController->showPeerHistory(
					peer,
					Window::SectionShow::Way::Forward);
			}
		};
		result.match([&](const MTPDupdates &data) {
			handleChats(data.vchats());
		}, [&](const MTPDupdatesCombined &data) {
			handleChats(data.vchats());
		}, [&](auto &&) {
			LOG(("API Error: unexpected update cons %1 "
				"(ApiWrap::importChatInvite)").arg(result.type()));
		});
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();

		const auto strongController = weak.get();
		if (!strongController) {
			return;
		} else if (type == u"CHANNELS_TOO_MUCH"_q) {
			strongController->show(
				Box(ChannelsLimitBox, &strongController->session()));
		}

		strongController->hideLayer();
		Ui::ShowMultilineToast({
			.parentOverride = Window::Show(strongController).toastParent(),
			.text = { [&] {
				if (type == u"INVITE_REQUEST_SENT"_q) {
					return isGroup
						? tr::lng_group_request_sent(tr::now)
						: tr::lng_group_request_sent_channel(tr::now);
				} else if (type == u"USERS_TOO_MUCH"_q) {
					return tr::lng_group_invite_no_room(tr::now);
				} else {
					return tr::lng_group_invite_bad_link(tr::now);
				}
			}() },
			.duration = ApiWrap::kJoinErrorDuration });
	}).send();
}

} // namespace

void CheckChatInvite(
		not_null<Window::SessionController*> controller,
		const QString &hash,
		ChannelData *invitePeekChannel) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	session->api().checkChatInvite(hash, [=](const MTPChatInvite &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		Core::App().hideMediaView();
		const auto show = [&](not_null<PeerData*> chat) {
			if (const auto forum = chat->forum()) {
				strong->openForum(
					forum->channel(),
					Window::SectionShow::Way::Forward);
			} else {
				strong->showPeerHistory(
					chat,
					Window::SectionShow::Way::Forward);
			}
		};
		result.match([=](const MTPDchatInvite &data) {
			const auto isGroup = !data.is_broadcast();
			const auto box = strong->show(Box<ConfirmInviteBox>(
				session,
				data,
				invitePeekChannel,
				[=] { SubmitChatInvite(weak, session, hash, isGroup); }));
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
				show(chat);
			}
		}, [=](const MTPDchatInvitePeek &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->setInvitePeek(hash, data.vexpires().v);
					show(chat);
				}
			}
		});
	}, [=](const MTP::Error &error) {
		if (error.code() != 400) {
			return;
		}
		Core::App().hideMediaView();
		if (const auto strong = weak.get()) {
			strong->show(Ui::MakeInformBox(tr::lng_group_invite_bad_link()));
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
: ConfirmInviteBox(
	session,
	Parse(session, data),
	invitePeekChannel,
	std::move(submit)) {
}

ConfirmInviteBox::ConfirmInviteBox(
	not_null<Main::Session*> session,
	ChatInvite &&invite,
	ChannelData *invitePeekChannel,
	Fn<void()> submit)
: _session(session)
, _submit(std::move(submit))
, _title(this, st::confirmInviteTitle)
, _status(this, st::confirmInviteStatus)
, _about(this, st::confirmInviteAbout)
, _aboutRequests(this, st::confirmInviteStatus)
, _participants(std::move(invite.participants))
, _isChannel(invite.isChannel && !invite.isMegagroup)
, _requestApprove(invite.isRequestNeeded) {
	const auto count = invite.participantsCount;
	const auto status = [&] {
		return invitePeekChannel
			? tr::lng_channel_invite_private(tr::now)
			: (!_participants.empty() && _participants.size() < count)
			? tr::lng_group_invite_members(tr::now, lt_count, count)
			: (count > 0 && _isChannel)
			? tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				count)
			: (count > 0)
			? tr::lng_chat_status_members(tr::now, lt_count_decimal, count)
			: _isChannel
			? tr::lng_channel_status(tr::now)
			: tr::lng_group_status(tr::now);
	}();
	_title->setText(invite.title);
	_status->setText(status);
	if (!invite.about.isEmpty()) {
		_about->setText(invite.about);
	} else {
		_about.destroy();
	}
	if (_requestApprove) {
		_aboutRequests->setText(_isChannel
			? tr::lng_group_request_about_channel(tr::now)
			: tr::lng_group_request_about(tr::now));
	} else {
		_aboutRequests.destroy();
	}

	if (invite.photo) {
		_photo = invite.photo->createMediaView();
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
			invite.title);
	}
}

ConfirmInviteBox::~ConfirmInviteBox() = default;

ConfirmInviteBox::ChatInvite ConfirmInviteBox::Parse(
		not_null<Main::Session*> session,
		const MTPDchatInvite &data) {
	auto participants = std::vector<Participant>();
	if (const auto list = data.vparticipants()) {
		participants.reserve(list->v.size());
		for (const auto &participant : list->v) {
			if (const auto user = session->data().processUser(participant)) {
				participants.push_back(Participant{ user });
			}
		}
	}
	const auto photo = session->data().processPhoto(data.vphoto());
	return {
		.title = qs(data.vtitle()),
		.about = data.vabout().value_or_empty(),
		.photo = (photo->isNull() ? nullptr : photo.get()),
		.participantsCount = data.vparticipants_count().v,
		.participants = std::move(participants),
		.isPublic = data.is_public(),
		.isChannel = data.is_channel(),
		.isMegagroup = data.is_megagroup(),
		.isBroadcast = data.is_broadcast(),
		.isRequestNeeded = data.is_request_needed(),
	};
}

void ConfirmInviteBox::prepare() {
	addButton(
		(_requestApprove
			? tr::lng_group_request_to_join()
			: _isChannel
			? tr::lng_profile_join_channel()
			: tr::lng_profile_join_group()),
		_submit);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	while (_participants.size() > 4) {
		_participants.pop_back();
	}

	auto newHeight = st::confirmInviteStatusTop + _status->height() + st::boxPadding.bottom();
	if (!_participants.empty()) {
		int skip = (st::confirmInviteUsersWidth - 4 * st::confirmInviteUserPhotoSize) / 5;
		int padding = skip / 2;
		_userWidth = (st::confirmInviteUserPhotoSize + 2 * padding);
		int sumWidth = _participants.size() * _userWidth;
		int left = (st::boxWideWidth - sumWidth) / 2;
		for (const auto &participant : _participants) {
			auto name = new Ui::FlatLabel(this, st::confirmInviteUserName);
			name->resizeToWidth(st::confirmInviteUserPhotoSize + padding);
			name->setText(participant.user->firstName.isEmpty()
				? participant.user->name()
				: participant.user->firstName);
			name->moveToLeft(left + (padding / 2), st::confirmInviteUserNameTop);
			left += _userWidth;
		}

		newHeight += st::confirmInviteUserHeight;
	}
	if (_about) {
		const auto padding = st::confirmInviteAboutPadding;
		_about->resizeToWidth(st::boxWideWidth - padding.left() - padding.right());
		newHeight += padding.top() + _about->height() + padding.bottom();
	}
	if (_aboutRequests) {
		const auto padding = st::confirmInviteAboutRequestsPadding;
		_aboutRequests->resizeToWidth(st::boxWideWidth - padding.left() - padding.right());
		newHeight += padding.top() + _aboutRequests->height() + padding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void ConfirmInviteBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_title->move((width() - _title->width()) / 2, st::confirmInviteTitleTop);
	_status->move((width() - _status->width()) / 2, st::confirmInviteStatusTop);
	auto bottom = _status->y()
		+ _status->height()
		+ st::boxPadding.bottom()
		+ (_participants.empty() ? 0 : st::confirmInviteUserHeight);
	if (_about) {
		const auto padding = st::confirmInviteAboutPadding;
		_about->move((width() - _about->width()) / 2, bottom + padding.top());
		bottom += padding.top() + _about->height() + padding.bottom();
	}
	if (_aboutRequests) {
		const auto padding = st::confirmInviteAboutRequestsPadding;
		_aboutRequests->move((width() - _aboutRequests->width()) / 2, bottom + padding.top());
	}
}

void ConfirmInviteBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_photo) {
		if (const auto image = _photo->image(Data::PhotoSize::Small)) {
			const auto size = st::confirmInvitePhotoSize;
			p.drawPixmap(
				(width() - size) / 2,
				st::confirmInvitePhotoTop,
				image->pix(
					{ size, size },
					{ .options = Images::Option::RoundCircle }));
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
