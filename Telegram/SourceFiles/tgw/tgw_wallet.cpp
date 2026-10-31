/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_wallet.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "tgw/tgw_bridge.h"
#include "twidget/sdk_policy.h"
#include "ui/dynamic_thumbnails.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_bot.h"
#include "wallet/wallet_ton_connect_request.h"
#include "window/window_session_controller.h"

namespace Tgw {
namespace {

using Failure = twidget::WidgetRequestFailure;
using Respond = Fn<void(const twidget::WidgetRequestOutcome&)>;

// WHY: after Confirm the query is the bot's due even when the widget is
// gone, so a call lives with the session, not with the widget's runtime.
class WalletCalls final : public base::has_weak_ptr {
public:
	explicit WalletCalls(not_null<Main::Session*> session);
	~WalletCalls();

	[[nodiscard]] bool busy() const;
	void run(
		not_null<HistoryItem*> item,
		not_null<UserData*> bot,
		std::shared_ptr<Main::SessionShow> show,
		const twidget::WalletCall &call,
		Respond respond);

private:
	struct Call {
		FullMsgId itemId;
		not_null<PeerData*> peer;
		MsgId msgId = 0;
		std::string data;
		Respond respond;
		std::unique_ptr<Wallet::TonConnectBotRequest> flow;
	};

	void removed(not_null<const HistoryItem*> item);
	void finished(Wallet::TonConnectBotOutcome outcome);
	void release();

	const not_null<Main::Session*> _session;
	BotQueries _queries;
	std::unique_ptr<Call> _active;
	std::unique_ptr<Call> _released;
	rpl::lifetime _lifetime;

};

using Registry = base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<WalletCalls>>;

[[nodiscard]] Registry &Sessions() {
	static auto result = Registry();
	return result;
}

[[nodiscard]] not_null<WalletCalls*> CallsOf(
		not_null<Main::Session*> session) {
	auto &sessions = Sessions();
	const auto i = sessions.find(session);
	if (i != end(sessions)) {
		return i->second.get();
	}
	session->lifetime().add([=] {
		Sessions().remove(session);
	});
	return sessions.emplace(
		session,
		std::make_unique<WalletCalls>(session)).first->second.get();
}

[[nodiscard]] twidget::WidgetRequestOutcome Outcome(Failure failure) {
	return { .failure = failure };
}

[[nodiscard]] Respond Once(Respond respond) {
	const auto shared = std::make_shared<Respond>(std::move(respond));
	return [=](const twidget::WidgetRequestOutcome &outcome) {
		if (const auto taken = base::take(*shared)) {
			taken(outcome);
		}
	};
}

[[nodiscard]] std::optional<QString> OptionalText(
		const std::optional<std::string> &value) {
	return value
		? std::make_optional(QString::fromStdString(*value))
		: std::nullopt;
}

[[nodiscard]] std::optional<TimeId> ValidUntil(
		const std::optional<int64_t> &value) {
	return value
		? std::make_optional(TimeId(std::clamp(
			*value,
			int64_t(std::numeric_limits<TimeId>::min()),
			int64_t(std::numeric_limits<TimeId>::max()))))
		: std::nullopt;
}

[[nodiscard]] Wallet::TonConnectBotTransaction TransactionFrom(
		const twidget::WalletTransaction &transaction) {
	auto result = Wallet::TonConnectBotTransaction{
		.validUntil = ValidUntil(transaction.validUntil),
		.network = OptionalText(transaction.network),
		.from = OptionalText(transaction.from),
	};
	result.messages.reserve(transaction.messages.size());
	for (const auto &message : transaction.messages) {
		result.messages.push_back({
			.address = QString::fromStdString(message.address),
			.amount = QString::fromStdString(message.amount),
			.payload = OptionalText(message.payload),
			.stateInit = OptionalText(message.stateInit),
		});
	}
	return result;
}

[[nodiscard]] MTPUserWallet UserWalletOf(
		const Wallet::TonConnectSignature &account,
		int64 balance) {
	return MTP_userWallet(
		MTP_flags(MTPDuserWallet::Flag::f_public_key),
		MTP_string(Wallet::FormatFriendly(account.address, false)),
		MTP_long(balance),
		MTP_bytes(account.publicKey),
		MTP_bytes(account.stateInit));
}

WalletCalls::WalletCalls(not_null<Main::Session*> session)
: _session(session)
, _queries(session) {
	session->data().itemRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		removed(item);
	}, _lifetime);
}

WalletCalls::~WalletCalls() {
	if (const auto call = base::take(_active)) {
		call->flow = nullptr;
		call->respond(Outcome(Failure::Failed));
	}
}

bool WalletCalls::busy() const {
	return (_active != nullptr);
}

void WalletCalls::run(
		not_null<HistoryItem*> item,
		not_null<UserData*> bot,
		std::shared_ptr<Main::SessionShow> show,
		const twidget::WalletCall &call,
		Respond respond) {
	const auto session = _session;
	const auto itemId = item->fullId();
	_active = std::make_unique<Call>(Call{
		.itemId = itemId,
		.peer = item->history()->peer,
		.msgId = item->id,
		.data = call.data,
		.respond = std::move(respond),
	});
	_active->flow = std::make_unique<Wallet::TonConnectBotRequest>(
		Wallet::TonConnectBotArgs{
			.show = std::move(show),
			.name = bot->name(),
			.username = bot->username(),
			.userpic = Ui::MakeUserpicThumbnail(bot, true),
			.transaction = TransactionFrom(call.transaction),
			.current = [=] {
				const auto message = session->data().message(itemId);
				return message && message->isRegular();
			},
			.done = crl::guard(this, [=](
					Wallet::TonConnectBotOutcome outcome) {
				finished(std::move(outcome));
			}),
		});
	const auto flow = _active->flow.get();
	flow->start();
}

void WalletCalls::removed(not_null<const HistoryItem*> item) {
	if (!_active
		|| (_active->itemId != item->fullId())
		|| !_active->flow->cancel()) {
		return;
	}
	release();
	_released->respond(Outcome(Failure::Failed));
}

void WalletCalls::finished(Wallet::TonConnectBotOutcome outcome) {
	using Result = Wallet::TonConnectBotResult;
	if (!_active) {
		return;
	}
	release();
	const auto call = _released.get();
	switch (outcome.result) {
	case Result::Declined:
		call->respond(Outcome(Failure::Rejected));
		return;
	case Result::Refused:
		call->respond(Outcome(Failure::BadRequest));
		return;
	case Result::Sent:
		_queries.send(
			call->peer,
			call->msgId,
			MTP_widgetQueryTypeSentTransaction(
				UserWalletOf(
					outcome.signature,
					_session->wallet().balanceNano()),
				MTP_string(outcome.messageHash.toStdString())),
			call->data,
			call->respond);
		return;
	case Result::Failed:
	case Result::Signed:
		break;
	}
	call->respond(Outcome(Failure::Failed));
}

void WalletCalls::release() {
	_released = base::take(_active);
	// The flow may still be inside its own done() callback.
	crl::on_main(this, [=] {
		_released = nullptr;
	});
}

} // namespace

UserData *QueryBot(not_null<HistoryItem*> item) {
	if (const auto bot = item->viaBot()) {
		return bot;
	}
	const auto sender = item->originalSender();
	const auto user = sender ? sender->asUser() : nullptr;
	return (user && user->isBot()) ? user : nullptr;
}

void RunWalletCall(
		not_null<HistoryItem*> item,
		not_null<QWidget*> window,
		const twidget::WalletCall &call,
		Fn<void(const twidget::WidgetRequestOutcome&)> respond) {
	auto once = Once(std::move(respond));
	if (call.method == twidget::WalletCall::Method::SignData) {
		// Not offered yet: a signData call opens nothing and signs nothing.
		once(Outcome(Failure::Failed));
		return;
	}
	const auto session = &item->history()->session();
	const auto controller = WindowController(window, session);
	const auto bot = QueryBot(item);
	const auto calls = CallsOf(session);
	if (!controller
		|| !Wallet::Available(session)
		|| !bot
		|| calls->busy()
		|| session->wallet().tonConnect().requests().showing()) {
		once(Outcome(Failure::Failed));
		return;
	}
	calls->run(item, bot, controller->uiShow(), call, std::move(once));
}

} // namespace Tgw
