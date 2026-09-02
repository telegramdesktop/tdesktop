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
#include "wallet/wallet_custody.h"
#include "wallet/wallet_stream.h"

namespace wallet_engine {
struct ActivityItem;
struct NftList;
struct SendSnapshot;
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
struct ShareFetch;
class UserAddresses;

enum class Presence {
	Unknown,
	Provisioning,
	Missing,
	Unavailable,
	AddressUnreadable,
	Ready,
};

struct WalletCapabilities {
	bool backupEnabled = false;
	bool canExportPhrase = false;
	bool canEnableBackup = false;

	friend bool operator==(
		const WalletCapabilities &,
		const WalletCapabilities &) = default;
};

enum class DeviceMode {
	Unknown,
	Full,
	ReadOnlyRestorable,
	ReadOnlyNotRestorable,
};

struct DeviceCustodyState {
	DeviceMode mode = DeviceMode::Unknown;
	bool conflict = false;

	friend bool operator==(
		const DeviceCustodyState &,
		const DeviceCustodyState &) = default;
};

// The two terms the overview's empty face takes from the lists gate. One
// updateListsGate() call can change both, and the face is derived through a
// single rpl::combine argument, so publishing them as one value is what
// keeps a face from being resolved once from a new term against a stale one
// before the second arrives.
struct ListsEmptyState {
	bool confirmedEmpty = false;
	bool unreachable = false;

	friend bool operator==(
		const ListsEmptyState &,
		const ListsEmptyState &) = default;
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
		KeyChange,
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

[[nodiscard]] bool IsWordlistWord(const QString &word);
[[nodiscard]] std::vector<QString> WordlistSuggestions(
	const QString &prefix,
	int limit);

class Session final {
public:
	explicit Session(not_null<Main::Session*> session);
	~Session();

	[[nodiscard]] Presence presence();
	[[nodiscard]] rpl::producer<Presence> presenceValue();
	[[nodiscard]] std::optional<QString> address();
	[[nodiscard]] QString addressFriendly(bool bounceable = false);
	[[nodiscard]] WalletCapabilities capabilities() const;
	[[nodiscard]] rpl::producer<WalletCapabilities> capabilitiesValue() const;
	[[nodiscard]] QByteArray publicKey() const;
	[[nodiscard]] bool revealsLocally();
	[[nodiscard]] DeviceCustodyState deviceCustodyState() const;
	[[nodiscard]] auto deviceCustodyStateValue() const
		-> rpl::producer<DeviceCustodyState>;
	[[nodiscard]] rpl::producer<> custodyUpdates() const;
	[[nodiscard]] std::vector<CustodyRecord> parkedRecords();

	void refreshState();
	void applyUpdate(const MTPDupdateWalletState &data);

	void revealPhrase(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail);
	void replaceWithNew(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void replaceWithImported(
		std::vector<QString> words,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void restoreFromPhrase(
		std::vector<QString> words,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void restoreFromBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void revealParked(
		const QByteArray &publicKey,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail);
	void dropParked(
		const QByteArray &publicKey,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void prepareBackupParts(
		Fn<void(std::vector<QByteArray>)> done,
		Fn<void(const QString &error)> fail);
	void enableBackup(
		std::vector<QByteArray> parts,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void disableBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	[[nodiscard]] bool rotationOffered();
	void quoteRotationFee(Fn<void(FeeResult)> done);
	void prepareRotation(
		int64 quotedFeeNano,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail);
	void abandonRotation();
	void submitRotation(
		Fn<void()> confirmed,
		Fn<void(const QString &error)> fail);

	[[nodiscard]] int64 balanceNano() const;
	[[nodiscard]] rpl::producer<int64> balanceNanoValue() const;
	[[nodiscard]] rpl::producer<bool> stateKnownValue() const;
	[[nodiscard]] AccountStatus status() const;
	[[nodiscard]] const std::vector<TransferItem> &history() const;
	[[nodiscard]] rpl::producer<> historyUpdates() const;
	[[nodiscard]] bool listsGated() const;
	[[nodiscard]] rpl::producer<bool> listsGatedValue() const;
	[[nodiscard]] rpl::producer<ListsEmptyState> listsEmptyStateValue() const;

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
	void revealLocally(
		const CustodyRecord &record,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail);
	void revealFromShares(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail);
	void fetchShareParts(
		const QString &token,
		std::vector<int> dcs,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail);
	void restoreFromWords(
		std::vector<QString> words,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail);
	[[nodiscard]] bool custodyBusy() const;
	[[nodiscard]] const CustodyStore &custody();
	[[nodiscard]] bool persistCustody(const CustodyRecord &record);
	void sendReplaceWallet(
		const MTPInputWalletReplacement &wallet,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const QString &)> fail);
	void finishConfirmedReplace(
		std::optional<CustodyRecord> oldRecord,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	void reconcileCustody();
	void updateDeviceCustodyState();
	void syncEngineClient();
	void removeCustodyRecord(const QByteArray &publicKey);
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
	void clearCollectibles();
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
	void applyRotationSnapshot(
		const wallet_engine::SendSnapshot &snapshot,
		bool journalAuthoritative);
	void storePendingRotation(
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	void discardPendingRotation();
	void promotePendingRotation();
	void finishRotation(const QString &error);
	void clearRotatedSinceBackup();

	const not_null<Main::Session*> _session;
	Api _api;
	MTP::Sender _stateApi;
	const std::unique_ptr<Engine> _engine;
	const std::unique_ptr<Rates> _rates;
	const std::unique_ptr<Onramp> _onramp;
	const std::unique_ptr<UserAddresses> _userAddresses;
	const std::unique_ptr<Stream> _stream;
	base::Timer _pollTimer;
	base::Timer _shareFetchTimer;
	std::weak_ptr<ShareFetch> _shareFetch;

	bool _loaded = false;
	QString _address;
	QByteArray _publicKey;

	rpl::variable<int64> _balanceNano = 0;
	rpl::variable<Presence> _presence = Presence::Unknown;
	rpl::variable<WalletCapabilities> _capabilities;
	std::optional<CustodyStore> _custody;
	bool _phraseRevealing = false;
	bool _replacing = false;
	bool _backupChanging = false;
	rpl::variable<DeviceCustodyState> _deviceCustody;
	rpl::event_stream<> _custodyUpdates;
	QString _clientRecordId;
	bool _clientStopping = false;
	AccountStatus _engineStatus = AccountStatus::NonExisting;
	mtpRequestId _stateRequestId = 0;
	crl::time _stateRequestedAt = 0;
	crl::time _stateRefreshedAt = 0;
	int _stateFailures = 0;
	// Set when the wallet cannot be read at all, from any of three sources:
	// a wallet state that stayed unknown for kStateFailuresBeforeStated reads,
	// a failed first transaction page of a wallet that does exist, or a wallet
	// state whose address the parser refused. All three make the overview
	// paint the unreachable face instead of the empty one.
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

	struct PreparedRotation;
	std::unique_ptr<PreparedRotation> _preparedRotation;
	bool _rotating = false;
	Fn<void()> _rotationConfirmed;
	Fn<void(const QString &)> _rotationFailed;

	std::unique_ptr<Ui::SeparatePanel> _panel;

};

} // namespace Wallet
