/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/channel_ownership_transfer.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "boxes/passcode_box.h"
#include "core/core_cloud_password.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"

ChannelOwnershipTransfer::ChannelOwnershipTransfer(
	not_null<PeerData*> peer,
	not_null<UserData*> selectedUser,
	std::shared_ptr<Ui::Show> show,
	Fn<void(std::shared_ptr<Ui::Show>)> onSuccess)
: _peer(peer)
, _selectedUser(selectedUser)
, _show(show)
, _onSuccess(std::move(onSuccess)) {
}

bool ChannelOwnershipTransfer::handleTransferPasswordError(
		const QString &error) {
	const auto session = &_selectedUser->session();
	auto about = (_peer->asChannel() && !_peer->isMegagroup()
		? tr::lng_rights_transfer_check_about_channel
		: tr::lng_rights_transfer_check_about)(
			tr::now,
			lt_user,
			tr::bold(_selectedUser->shortName()),
			tr::marked);
	if (auto box = PrePasswordErrorBox(error, session, std::move(about))) {
		_show->showBox(std::move(box));
		return true;
	}
	return false;
}

void ChannelOwnershipTransfer::start() {
	const auto api = &_peer->session().api();
	api->cloudPassword().reload();
	api->request(MTPmessages_EditChatCreator(
		_peer->input(),
		MTP_inputUserEmpty(),
		MTP_inputCheckPasswordEmpty()
	)).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		if (handleTransferPasswordError(type)) {
			return;
		}
		const auto callback = crl::guard(_show->toastParent(), [=](
				Fn<void()> &&close) {
			requestPassword();
			close();
		});
		_show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_rights_transfer_about(
				tr::now,
				lt_group,
				tr::bold(_peer->name()),
				lt_user,
				tr::bold(_selectedUser->shortName()),
				tr::rich),
			.confirmed = callback,
			.confirmText = tr::lng_rights_transfer_sure(),
		}));
	}).send();
}

void ChannelOwnershipTransfer::requestPassword() {
	_peer->session().api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_rights_transfer_password_title();
		fields.customDescription
			= tr::lng_rights_transfer_password_description(tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> box) {
			sendRequest(box, result);
		};
		_show->showBox(Box<PasscodeBox>(&_peer->session(), fields));
	}, _lifetime);
}

void ChannelOwnershipTransfer::sendRequest(
		base::weak_qptr<PasscodeBox> box,
		const Core::CloudPasswordResult &result) {
	const auto api = &_peer->session().api();
	api->request(MTPmessages_EditChatCreator(
		_peer->input(),
		_selectedUser->inputUser(),
		result.result
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
		const auto currentShow = box ? box->uiShow() : _show;
		currentShow->showToast(
			(_peer->isBroadcast()
				? tr::lng_rights_transfer_done_channel
				: tr::lng_rights_transfer_done_group)(
					tr::now,
					lt_user,
					_selectedUser->shortName()));
		if (_onSuccess) {
			_onSuccess(currentShow);
		}
	}).fail([=](const MTP::Error &error) {
		if (box && box->handleCustomCheckError(error)) {
			return;
		}
		const auto &type = error.type();
		const auto problem = [&] {
			if (type == u"CHANNELS_ADMIN_PUBLIC_TOO_MUCH"_q) {
				return tr::lng_channels_too_much_public_other(tr::now);
			} else if (type == u"CHANNELS_ADMIN_LOCATED_TOO_MUCH"_q) {
				return tr::lng_channels_too_much_located_other(tr::now);
			} else if (type == u"ADMINS_TOO_MUCH"_q) {
				return (_peer->isBroadcast()
					? tr::lng_error_admin_limit_channel
					: tr::lng_error_admin_limit)(tr::now);
			} else if (type == u"CHANNEL_INVALID"_q
				|| type == u"CHAT_CREATOR_REQUIRED"_q
				|| type == u"PARTICIPANT_MISSING"_q) {
				return (_peer->isBroadcast()
					? tr::lng_channel_not_accessible
					: tr::lng_group_not_accessible)(tr::now);
			}
			return Lang::Hard::ServerError();
		}();
		_show->showBox(Ui::MakeInformBox(problem));
		if (box) {
			box->closeBox();
		}
	}).handleFloodErrors().send();
}
