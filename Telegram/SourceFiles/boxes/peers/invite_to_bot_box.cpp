/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/invite_to_bot_box.h"

#include "apiwrap.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"

namespace {

constexpr auto kInviteToBotLimit = 10;

class InviteToBotController final : public ChatsListBoxController {
public:
	explicit InviteToBotController(not_null<Main::Session*> session);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;

private:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void updateTitle();

	const not_null<Main::Session*> _session;

};

InviteToBotController::InviteToBotController(
	not_null<Main::Session*> session)
: ChatsListBoxController(session)
, _session(session) {
}

Main::Session &InviteToBotController::session() const {
	return *_session;
}

void InviteToBotController::rowClicked(not_null<PeerListRow*> row) {
	const auto count = delegate()->peerListSelectedRowsCount();
	if (row->checked() || count < kInviteToBotLimit) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		updateTitle();
	} else {
		delegate()->peerListUiShow()->showToast(tr::lng_bot_invite_limit(
			tr::now,
			lt_count,
			kInviteToBotLimit));
	}
}

void InviteToBotController::itemDeselectedHook(not_null<PeerData*> peer) {
	updateTitle();
}

void InviteToBotController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_bot_invite_title());
	updateTitle();
}

auto InviteToBotController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	const auto user = history->peer->asUser();
	if (!user
		|| user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	return std::make_unique<Row>(history);
}

void InviteToBotController::updateTitle() {
	const auto count = delegate()->peerListSelectedRowsCount();
	delegate()->peerListSetAdditionalTitle(
		rpl::single(u"%1 / %2"_q.arg(count).arg(kInviteToBotLimit)));
}

void ShowInviteError(
		const std::shared_ptr<Main::SessionShow> &show,
		const std::vector<not_null<UserData*>> &users,
		const MTP::Error &error) {
	const auto &type = error.type();
	if (MTP::IgnoreError(error)) {
		return;
	} else if (type == u"BOT_INVITE_ALREADY_STARTED"_q) {
		show->showToast((users.size() == 1)
			? tr::lng_bot_invite_already_one(
				tr::now,
				lt_user,
				users.front()->name())
			: tr::lng_bot_invite_already_many(tr::now));
	} else if (type == u"BOT_INVITE_FAILED"_q) {
		show->showToast(tr::lng_bot_invite_failed(tr::now));
	} else {
		MTP::ShowErrorFallback(show, error);
	}
}

void SubmitInvite(
		not_null<PeerListBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> bot,
		not_null<bool*> pending) {
	if (*pending) {
		return;
	}
	auto users = std::vector<not_null<UserData*>>();
	auto inputs = QVector<MTPInputUser>();
	for (const auto &peer : box->collectSelectedRows()) {
		if (const auto user = peer->asUser()) {
			users.push_back(user);
			inputs.push_back(user->inputUser());
		}
	}
	if (users.empty()) {
		return;
	}
	*pending = true;
	const auto close = crl::guard(box, [=] {
		box->closeBox();
	});
	const auto unlock = crl::guard(box, [=] {
		*pending = false;
	});
	bot->session().api().request(MTPmessages_InviteUsersToBot(
		bot->inputUser(),
		MTP_vector<MTPInputUser>(std::move(inputs))
	)).done([=](const MTPBool &result) {
		if (mtpIsTrue(result)) {
			show->showToast((users.size() == 1)
				? tr::lng_bot_invite_sent_one(tr::now)
				: tr::lng_bot_invite_sent_many(tr::now));
			close();
		} else {
			show->showToast(tr::lng_bot_invite_failed(tr::now));
			unlock();
		}
	}).fail([=](const MTP::Error &error) {
		ShowInviteError(show, users, error);
		unlock();
	}).send();
}

} // namespace

bool CanInviteToBot(not_null<UserData*> bot) {
	return bot->botInfo && bot->botInfo->canEditInformation;
}

void ShowInviteToBotBox(
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> bot) {
	if (!CanInviteToBot(bot) || show->showFrozenError()) {
		return;
	}
	auto initBox = [=](not_null<PeerListBox*> box) {
		const auto pending = box->lifetime().make_state<bool>(false);
		box->addButton(tr::lng_bot_invite_button(), [=] {
			SubmitInvite(box, show, bot, pending);
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	};
	show->showBox(Box<PeerListBox>(
		std::make_unique<InviteToBotController>(&bot->session()),
		std::move(initBox)));
}
