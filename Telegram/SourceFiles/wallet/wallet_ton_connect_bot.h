/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_request_box.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class DynamicImage;
class GenericBox;
} // namespace Ui

namespace Wallet {

struct TonConnectBotMessage {
	QString address;
	QString amount;
	std::optional<QString> payload;
	std::optional<QString> stateInit;
};

struct TonConnectBotTransaction {
	std::optional<TimeId> validUntil;
	std::optional<QString> network;
	std::optional<QString> from;
	std::vector<TonConnectBotMessage> messages;
};

struct TonConnectBotSignData {
	TonConnectSignDataType type = TonConnectSignDataType::Text;
	QString text;
	QString bytes;
	QString schema;
	QString cell;
	std::optional<QString> network;
	std::optional<QString> from;
};

enum class TonConnectBotResult : uchar {
	Declined,
	Refused,
	Failed,
	Sent,
	Signed,
};

struct TonConnectBotOutcome {
	TonConnectBotResult result = TonConnectBotResult::Failed;
	QByteArray messageHash;
	TonConnectSignature signature;
};

struct TonConnectBotArgs {
	std::shared_ptr<Main::SessionShow> show;
	QString name;
	QString username;
	std::shared_ptr<Ui::DynamicImage> userpic;
	std::optional<TonConnectBotTransaction> transaction;
	std::optional<TonConnectBotSignData> signData;
	QString domain;
	Fn<bool()> current;
	Fn<void(TonConnectBotOutcome)> done;
};

// done runs at most once, maybe inside start(); cancel() acts before the send.
class TonConnectBotRequest final : public base::has_weak_ptr {
public:
	explicit TonConnectBotRequest(TonConnectBotArgs args);
	~TonConnectBotRequest();

	void start();
	[[nodiscard]] bool cancel();

private:
	[[nodiscard]] TonConnectRequestBoxState stateFor(
		TonConnectRequestPhase phase) const;
	[[nodiscard]] bool showBox();
	void waitWallet();
	void resolve();
	void stopResolving();
	void prepare();
	void preview();
	void previewed(FeeResult result);
	void armDeadline();
	void expired();
	void confirmPressed();
	void keyReady(KeyAuthorization auth);
	void accountReady(TonConnectSignature account);
	void dataSigned(TonConnectSignature result);
	void signFailed(TonConnectKeyError error);
	void sent(TonConnectSendResult result);
	void declinePressed();
	void dismissed();
	void backToConfirm(const QString &error);
	void refuse();
	void fail();
	void closeWithToast(const QString &text, TonConnectBotOutcome outcome);
	void closeBox();
	void finish(TonConnectBotOutcome outcome);
	void stop();

	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<Main::Session*> _session;
	const QString _name;
	const QString _domain;
	const std::shared_ptr<Ui::DynamicImage> _userpic;
	const std::optional<TonConnectBotTransaction> _transaction;
	const std::optional<TonConnectBotSignData> _signData;
	const QString _signDomain;
	const Fn<bool()> _current;
	const Fn<void(TonConnectBotOutcome)> _done;
	const std::string _operationId;
	base::weak_qptr<Ui::GenericBox> _box;
	rpl::variable<TonConnectRequestBoxState> _state;
	std::shared_ptr<const TonConnectTransfer> _transfer;
	std::shared_ptr<const PreparedSend> _prepared;
	std::shared_ptr<const TonConnectSignData> _signRequest;
	KeyAuthorization _auth;
	TonConnectSignature _account;
	uint64 _previewOwner = 0;
	bool _polling = false;
	bool _closingBox = false;
	bool _sendStarted = false;
	bool _handoffRefused = false;
	bool _finished = false;
	base::Timer _deadlineTimer;
	base::Timer _resolveTimer;
	rpl::lifetime _resolveLifetime;
	rpl::lifetime _previewLifetime;
	rpl::lifetime _idleLifetime;
	rpl::lifetime _keyLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
