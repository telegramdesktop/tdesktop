/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/timer.h"
#include "core/core_cloud_password.h"
#include "gram/api/gram_api_nft.h"
#include "mtproto/sender.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_stream.h"

namespace wallet_engine {
struct ActivityItem;
struct NftList;
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
class UserAddresses;

enum class Presence {
	Unknown,
	Provisioning,
	Missing,
	Unavailable,
	Ready,
};

struct WalletCapabilities {
	bool backupEnabled = false;
	bool canExportPhrase = false;
};

enum class AccountStatus {
	NonExisting,
	Uninit,
	Active,
	Frozen,
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

struct TransferItem {
	enum class Status {
		Success,
		Failure,
		Pending,
	};
	enum class Kind {
		Transfer,
		ContractInteraction,
		Collectible,
		CardTopUp,
		PeerTransfer,
	};

	Kind kind = Kind::Transfer;
	bool incoming = false;
	QString counterparty;
	QString counterpartyName;
	quint64 counterpartyPeer = 0;
	QString collectible;
	QString collectibleName;
	QString collectibleImageUrl;
	QString provider;
	int64 amountNano = 0;
	int64 feeNano = 0;
	QString comment;
	bool commentEncrypted = false;
	QByteArray encryptedPayload;
	TimeId date = 0;
	quint64 lt = 0;
	QByteArray traceId;
	QByteArray externalHashNorm;
	Status status = Status::Success;

	friend bool operator==(
		const TransferItem &,
		const TransferItem &) = default;
};

struct FeeResult {
	int64 feeNano = 0;
	SendError error = SendError::None;
};

struct PendingSendInfo {
	TimeId posted = 0;
	int64 amountNano = 0;
	QString destination;
	QString comment;
};

struct SendArgs {
	QString destination;
	int64 amountNano = 0;
	QString comment;
	bool bounce = true;
};

[[nodiscard]] std::vector<TransferItem> HistoryFromEngine(
	const std::vector<wallet_engine::ActivityItem> &items);

[[nodiscard]] std::vector<TransferItem> HistoryFromServer(
	const QVector<MTPWalletTransaction> &list);

[[nodiscard]] std::vector<Gram::NftItem> CollectiblesFromEngine(
	const wallet_engine::NftList &list);

class Session final {
public:
	explicit Session(not_null<Main::Session*> session);
	~Session();

	[[nodiscard]] Presence presence();
	[[nodiscard]] rpl::producer<Presence> presenceValue();
	[[nodiscard]] std::optional<QString> address();
	[[nodiscard]] QString addressFriendly(bool bounceable = false);
	[[nodiscard]] WalletCapabilities capabilities() const;

	void refreshState();
	void applyUpdate(const MTPDupdateWalletState &data);

	void revealPhrase(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail);

	[[nodiscard]] int64 balanceNano() const;
	[[nodiscard]] rpl::producer<int64> balanceNanoValue() const;
	[[nodiscard]] rpl::producer<bool> stateKnownValue() const;
	[[nodiscard]] AccountStatus status() const;
	[[nodiscard]] const std::vector<TransferItem> &history() const;
	[[nodiscard]] rpl::producer<> historyUpdates() const;
	[[nodiscard]] bool listsGated() const;
	[[nodiscard]] rpl::producer<bool> listsGatedValue() const;
	[[nodiscard]] rpl::producer<bool> listsConfirmedEmptyValue() const;
	[[nodiscard]] rpl::producer<bool> stateUnreachableValue() const;

	void refreshHistory(Fn<void()> done = nullptr);
	[[nodiscard]] bool historyHasNext() const;
	void loadMoreHistory();

	[[nodiscard]] const std::vector<Gram::NftItem> &collectibles() const;
	[[nodiscard]] rpl::producer<> collectiblesUpdates() const;
	[[nodiscard]] bool collectiblesTab() const;
	[[nodiscard]] rpl::producer<bool> collectiblesTabValue() const;
	void setCollectiblesTab(bool value);
	[[nodiscard]] bool collectiblesHasNext() const;
	void loadMoreCollectibles();
	void resolveCollectibleInfo(
		const QString &item,
		Fn<void(const Gram::NftItem &)> done);
#ifdef _DEBUG
	void debugClearNetworkState();
	void debugRestoreNetworkState();
#endif

	void startPolling();
	void stopPolling();
	[[nodiscard]] bool pollingRequested() const;

	[[nodiscard]] Onramp &onramp();
	[[nodiscard]] Rates &rates();
	[[nodiscard]] UserAddresses &userAddresses();

	[[nodiscard]] Ui::SeparatePanel *panel() const;
	void setPanel(std::unique_ptr<Ui::SeparatePanel> panel);

	void estimateFee(const SendArgs &args, Fn<void(FeeResult)> done);
	void send(SendArgs args, Fn<void(SendError)> done);
	[[nodiscard]] SendState sendState() const;
	[[nodiscard]] rpl::producer<SendState> sendStateValue() const;
	[[nodiscard]] const std::optional<PendingSendInfo> &pendingSend() const;

private:
	void ensureLoaded();
	void requestState();
	void applyState(const MTPWalletState &state);
	void setPresence(Presence presence);
	void clearNetworkState();
	void pollTick();
	void updatePollingState();
	void applyStreamRefresh(StreamRefresh wanted);
	void requestEngineRefresh();
	void applyEngineUpdate(const wallet_engine::WalletUpdate &update);
	void setHistory(std::vector<TransferItem> &&list);
	void requestTransactions(bool more);
	void applyTransactions(const MTPwallet_Transactions &result, bool more);
	void finishHistoryWaiters();
	void clearHistory();
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
	void updateListsGate();
	[[nodiscard]] bool listsConfirmedEmpty() const;
	void finishPending();

	const not_null<Main::Session*> _session;
	Api _api;
	MTP::Sender _stateApi;
	const std::unique_ptr<Engine> _engine;
	const std::unique_ptr<Rates> _rates;
	const std::unique_ptr<Onramp> _onramp;
	const std::unique_ptr<UserAddresses> _userAddresses;
	const std::unique_ptr<Stream> _stream;
	base::Timer _pollTimer;

	bool _loaded = false;
	QString _address;

	rpl::variable<int64> _balanceNano = 0;
	rpl::variable<Presence> _presence = Presence::Unknown;
	WalletCapabilities _capabilities;
	AccountStatus _engineStatus = AccountStatus::NonExisting;
	mtpRequestId _stateRequestId = 0;
	crl::time _stateRequestedAt = 0;
	crl::time _stateRefreshedAt = 0;
	int _stateFailures = 0;
	// Set when the wallet cannot be read at all, from either of two sources:
	// a wallet state that stayed unknown for kStateFailuresBeforeStated reads,
	// or a failed first transaction page of a wallet that does exist. Both
	// make the overview paint the unreachable face instead of the empty one.
	// The public accessor keeps the state-only name it shipped with.
	bool _stateUnreachable = false;
	std::vector<TransferItem> _history;
	rpl::event_stream<> _historyUpdates;
	bool _historyHasNext = false;
	crl::time _historyRefreshedAt = 0;

	std::vector<Gram::NftItem> _collectibles;
	rpl::event_stream<> _collectiblesUpdates;
	rpl::variable<bool> _collectiblesTab = false;
	crl::time _collectiblesRefreshedAt = 0;
	crl::time _collectiblesCompletedAt = 0;
	bool _collectiblesRequestPending = false;
	bool _collectiblesHasMore = false;
	bool _collectiblesPaged = false;
	base::flat_map<QString, Gram::NftItem> _collectibleInfo;
	base::flat_map<
		QString,
		std::vector<Fn<void(const Gram::NftItem &)>>> _collectibleInfoWaiters;

	rpl::variable<bool> _listsGated = true;
	rpl::event_stream<> _listsStateUpdates;
#ifdef _DEBUG
	int _debugClearedPollingCount = 0;
#endif // _DEBUG

	int _pollingCount = 0;
	int _networkGeneration = 0;
	mtpRequestId _historyRequestId = 0;
	bool _resolveRequestPending = false;
	bool _engineRefreshPending = false;
	bool _historySettled = false;
	bool _historyUnreachable = false;
	bool _historyPaged = false;
	crl::time _historyRequestedAt = 0;
	QString _historyNextOffset;
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
