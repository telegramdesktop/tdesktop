/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_history.h"
#include "gram/crypto/gram_ed25519.h"
#include "gram/crypto/gram_mnemonic.h"
#include "gram/ton/gram_address.h"
#include "gram/wallet/gram_wallet_v5.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_fee_estimator.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Wallet {

class Rates;

enum class KeyState {
	None,
	Created,
	Imported,
};

enum class SendState {
	Idle,
	Sending,
	Pending,
	Failed,
};

struct PendingSend {
	QByteArray messageHashNorm;
	quint32 signedSeqno = 0;
	TimeId validUntil = 0;
	TimeId posted = 0;
	int64 amountNano = 0;
	Gram::Address destination;
	QString comment;
};

struct SendArgs {
	Gram::Address destination;
	int64 amountNano = 0;
	QString comment;
	bool bounce = true;
	bool simulateStaleSeqno = false;
};

class Session final {
public:
	explicit Session(not_null<Main::Session*> session);
	~Session();

	[[nodiscard]] KeyState keyState();
	[[nodiscard]] rpl::producer<KeyState> keyStateValue();
	[[nodiscard]] std::optional<Gram::Address> address();
	[[nodiscard]] QString addressFriendly(bool bounceable = false);

	bool create();
	bool import(std::vector<QString> words);
	void remove();

	[[nodiscard]] bool phraseUnviewed();
	[[nodiscard]] rpl::producer<bool> phraseUnviewedValue();
	void markPhraseViewed();

	[[nodiscard]] int64 balanceNano() const;
	[[nodiscard]] rpl::producer<int64> balanceNanoValue() const;
	[[nodiscard]] rpl::producer<bool> stateKnownValue() const;
	[[nodiscard]] Gram::AccountStatus status() const;
	[[nodiscard]] const std::vector<Gram::TransferItem> &history() const;
	[[nodiscard]] rpl::producer<> historyUpdates() const;

	void refreshState(
		Fn<void(const Gram::AccountState &)> done = nullptr,
		Fn<void(const Gram::ApiError &)> fail = nullptr);
	void refreshHistory(Fn<void()> done = nullptr);
	[[nodiscard]] bool historyHasNext() const;
	void loadMoreHistory();
#ifdef _DEBUG
	void injectDebugHistory(std::vector<Gram::TransferItem> items);
	void debugRawRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail);
#endif

	void startPolling();
	void stopPolling();
	[[nodiscard]] bool pollingRequested() const;

	[[nodiscard]] Rates &rates();

	[[nodiscard]] Ui::SeparatePanel *panel() const;
	void setPanel(std::unique_ptr<Ui::SeparatePanel> panel);

	void estimateFee(const SendArgs &args, Fn<void(FeeResult)> done);
	void send(SendArgs args, Fn<void(QString)> done);
	[[nodiscard]] SendState sendState() const;
	[[nodiscard]] rpl::producer<SendState> sendStateValue() const;
	[[nodiscard]] const std::optional<PendingSend> &pendingSend() const;

private:
	void ensureLoaded();
	bool applyKey(
		std::vector<QString> words,
		Gram::MnemonicType type,
		quint32 walletId,
		KeyState state);
	void clearNetworkState();
	void pollTick();
	void updatePollingState();
	void applyAccountState(const Gram::AccountState &state);
	void mergeHistory(std::vector<Gram::TransferItem> &&items);
	void requestHistory(int offset);
	void requestHistoryFallback(int offset);
	void applyHistoryPage(int offset, Gram::HistoryPage &&page);
	void checkPendingByMessage();
	void checkPendingBySeqno(const Gram::AccountState &state);
	void finishPending();
	[[nodiscard]] bool pendingExpired() const;
	void sendWithState(
		SendArgs args,
		const Gram::AccountState &state,
		Fn<void(QString)> done);
	[[nodiscard]] Gram::TransferRequest buildTransferRequest(
		const SendArgs &args,
		quint32 seqno) const;

	const not_null<Main::Session*> _session;
	Api _api;
	const std::unique_ptr<FeeEstimator> _feeEstimator;
	const std::unique_ptr<Rates> _rates;
	base::Timer _pollTimer;

	bool _loaded = false;
	rpl::variable<KeyState> _keyState = KeyState::None;
	rpl::variable<bool> _phraseUnviewed = false;
	std::optional<Gram::KeyPair> _keyPair;
	Gram::Address _address;
	quint32 _walletId = Gram::kDefaultWalletId;

	rpl::variable<int64> _balanceNano = 0;
	rpl::variable<bool> _stateKnown = false;
	Gram::AccountState _lastState;
	std::vector<Gram::TransferItem> _history;
	rpl::event_stream<> _historyUpdates;
	bool _historyErrorLogged = false;
	bool _historyHasNext = false;
	int _historyLoadedOffset = 0;

	int _pollingCount = 0;
	bool _stateRequestPending = false;
	bool _historyRequestPending = false;
	bool _pendingCheckPending = false;
	std::vector<Fn<void(const Gram::AccountState &)>> _stateDone;
	std::vector<Fn<void(const Gram::ApiError &)>> _stateFail;
	std::vector<Fn<void()>> _historyDone;

	rpl::variable<SendState> _sendState = SendState::Idle;
	std::optional<PendingSend> _pending;

	std::unique_ptr<Ui::SeparatePanel> _panel;

};

} // namespace Wallet
