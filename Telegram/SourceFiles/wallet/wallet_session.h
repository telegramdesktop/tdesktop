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
#include "gram/ton/gram_address.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_stream.h"

namespace wallet_engine {
struct ActivityItem;
struct NftList;
struct WalletDescriptor;
struct WalletUpdate;
} // namespace wallet_engine

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Wallet {

class Engine;
class Onramp;
class Rates;

enum class KeyState {
	None,
	Created,
	Imported,
};

enum class LifecycleError {
	None,
	InvalidPhrase,
	Failed,
};

enum class SendError {
	None,
	InvalidRequest,
	InsufficientBalance,
	InsufficientFees,
	PreviousUnresolved,
	AlreadySending,
	SigningUnavailable,
	Failed,
};

enum class SendState {
	Idle,
	Sending,
	Pending,
};

struct FeeResult {
	int64 feeNano = 0;
	SendError error = SendError::None;
};

struct PendingSendInfo {
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
};

[[nodiscard]] std::vector<Gram::TransferItem> HistoryFromEngine(
	const std::vector<wallet_engine::ActivityItem> &items);

[[nodiscard]] std::vector<Gram::NftItem> CollectiblesFromEngine(
	const wallet_engine::NftList &list);

class Session final {
public:
	explicit Session(not_null<Main::Session*> session);
	~Session();

	[[nodiscard]] KeyState keyState();
	[[nodiscard]] rpl::producer<KeyState> keyStateValue();
	[[nodiscard]] std::optional<Gram::Address> address();
	[[nodiscard]] QString addressFriendly(bool bounceable = false);

	void create(Fn<void(LifecycleError)> done);
	void import(std::vector<QString> words, Fn<void(LifecycleError)> done);
	void remove(Fn<void(LifecycleError)> done);
	void revealPhrase(
		Fn<void(std::vector<QString>)> done,
		Fn<void(LifecycleError)> fail);
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
	void send(SendArgs args, Fn<void(SendError)> done);
	[[nodiscard]] SendState sendState() const;
	[[nodiscard]] rpl::producer<SendState> sendStateValue() const;
	[[nodiscard]] const std::optional<PendingSendInfo> &pendingSend() const;

private:
	void ensureLoaded();
	bool applyDescriptor(
		wallet_engine::WalletDescriptor descriptor,
		KeyState state);
	void clearNetworkState();
	void pollTick();
	void updatePollingState();
	void applyStreamRefresh(StreamRefresh wanted);
	void updateEngineClient();
	void requestEngineRefresh();
	void applyEngineUpdate(const wallet_engine::WalletUpdate &update);
	void requestMoreActivity();
	void applyEngineActivity(
		const wallet_engine::WalletUpdate &update,
		bool more);
	void setHistory(std::vector<Gram::TransferItem> &&list);
	void refreshCollectibles(bool force = false);
	void requestCollectibles(bool more);
	void applyCollectiblesUpdate(
		const wallet_engine::WalletUpdate &update,
		bool more);
	void setCollectibles(std::vector<Gram::NftItem> &&list);
	void startPreview(
		SendArgs args,
		Fn<void(FeeResult)> done,
		bool retried = false);
	void resolvePending();
	void finishPending();

	const not_null<Main::Session*> _session;
	Api _api;
	const std::unique_ptr<Engine> _engine;
	const std::unique_ptr<Rates> _rates;
	const std::unique_ptr<Onramp> _onramp;
	const std::unique_ptr<Stream> _stream;
	base::Timer _pollTimer;

	bool _loaded = false;
	rpl::variable<KeyState> _keyState = KeyState::None;
	rpl::variable<bool> _phraseUnviewed = false;
	std::unique_ptr<wallet_engine::WalletDescriptor> _descriptor;
	Gram::Address _address;

	rpl::variable<int64> _balanceNano = 0;
	rpl::variable<bool> _stateKnown = false;
	Gram::AccountStatus _engineStatus = Gram::AccountStatus::NonExisting;
	crl::time _stateRefreshedAt = 0;
	std::vector<Gram::TransferItem> _history;
	rpl::event_stream<> _historyUpdates;
	bool _historyHasNext = false;
	crl::time _historyRefreshedAt = 0;

	std::vector<Gram::NftItem> _collectibles;
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
	bool _historyInjected = false;
	bool _collectiblesInjected = false;
	int _debugClearedPollingCount = 0;
#endif // _DEBUG

	int _pollingCount = 0;
	int _networkGeneration = 0;
	bool _stateRequestPending = false;
	bool _historyRequestPending = false;
	bool _resolveRequestPending = false;
	QString _engineClientAddress;
	bool _engineStopping = false;
	bool _engineRefreshPending = false;
	bool _lifecyclePending = false;
	std::vector<Fn<void(const Gram::AccountState &)>> _stateDone;
	std::vector<Fn<void(const Gram::ApiError &)>> _stateFail;
	std::vector<Fn<void()>> _historyDone;

	rpl::variable<SendState> _sendState = SendState::Idle;
	std::optional<PendingSendInfo> _pending;
	bool _sendUnresolved = false;
	bool _previewPending = false;
	std::optional<SendArgs> _previewNextArgs;
	Fn<void(FeeResult)> _previewNextDone;

	std::unique_ptr<Ui::SeparatePanel> _panel;

};

} // namespace Wallet
