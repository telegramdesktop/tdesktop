/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/timer.h"
#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_history.h"
#include "gram/api/gram_api_nft.h"
#include "gram/crypto/gram_ed25519.h"
#include "gram/crypto/gram_mnemonic.h"
#include "gram/ton/gram_address.h"
#include "gram/wallet/gram_wallet_v5.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_fee_estimator.h"
#include "wallet/wallet_stream.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Wallet {

class Onramp;
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
	[[nodiscard]] bool provenEmpty() const;

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

	[[nodiscard]] const std::vector<Gram::NftItem> &collectibles() const;
	[[nodiscard]] rpl::producer<> collectiblesUpdates() const;
	[[nodiscard]] bool collectiblesTab() const;
	[[nodiscard]] rpl::producer<bool> collectiblesTabValue() const;
	void setCollectiblesTab(bool value);
	void resolveCollectibleInfo(
		const Gram::Address &item,
		Fn<void(const Gram::NftItem &)> done);
#ifdef _DEBUG
	void injectDebugHistory(std::vector<Gram::TransferItem> items);
	void injectDebugCollectibles(std::vector<Gram::NftItem> items);
	void debugRawRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail);
	void debugProductRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail);
	void debugStallNextRequest(
		const QString &endpoint,
		Fn<void()> swallowed);
	void debugReleaseStalledAnswer();
	void debugClearNetworkState();
	void debugRestoreNetworkState();
	void debugSetRefreshAges(crl::time age);
	[[nodiscard]] int debugPendingCount() const;
	void debugStreamUseFakeEndpoint();
	void debugStreamFailAcquires(bool fail);
	void debugStreamDeliverFrame(const QByteArray &frame);
	void debugStreamDropConnection();
	void debugStreamExpireNow();
	[[nodiscard]] bool debugStreamHealthy() const;
	[[nodiscard]] int debugStreamAcquireCount() const;
#endif

	void startPolling();
	void stopPolling();
	[[nodiscard]] bool pollingRequested() const;

	[[nodiscard]] Onramp &onramp();
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
	void applyStreamRefresh(StreamRefresh wanted);
	void applyAccountState(const Gram::AccountState &state);
	void mergeHistory(std::vector<Gram::TransferItem> &&items);
	void requestHistory(int offset);
	void requestHistoryFallback(int offset);
	void applyHistoryPage(int offset, Gram::HistoryPage &&page);
	void refreshCollectibles(bool force = false);
	void requestCollectibles(int offset);
	void applyCollectiblesPage(int offset, Gram::NftPage &&page);
	void setCollectibles(std::vector<Gram::NftItem> &&list);
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
	const std::unique_ptr<Onramp> _onramp;
	const std::unique_ptr<Stream> _stream;
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
	crl::time _stateRefreshedAt = 0;
	std::vector<Gram::TransferItem> _history;
	rpl::event_stream<> _historyUpdates;
	bool _historyErrorLogged = false;
	bool _historyHasNext = false;
	int _historyLoadedOffset = 0;
	crl::time _historyRefreshedAt = 0;

	std::vector<Gram::NftItem> _collectibles;
	std::vector<Gram::NftItem> _collectiblesLoading;
	rpl::event_stream<> _collectiblesUpdates;
	rpl::variable<bool> _collectiblesTab = false;
	crl::time _collectiblesRefreshedAt = 0;
	crl::time _collectiblesCompletedAt = 0;
	bool _collectiblesRequestPending = false;
	base::flat_map<QString, Gram::NftItem> _collectibleInfo;
	base::flat_map<
		QString,
		std::vector<Fn<void(const Gram::NftItem &)>>> _collectibleInfoWaiters;
#ifdef _DEBUG
	bool _collectiblesInjected = false;
	int _debugClearedPollingCount = 0;
#endif // _DEBUG

	int _pollingCount = 0;
	int _networkGeneration = 0;
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
