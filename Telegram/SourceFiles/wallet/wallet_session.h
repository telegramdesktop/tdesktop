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
#include "wallet/wallet_unlock.h"

namespace wallet_engine {
struct ActivityItem;
struct NftList;
struct SendSnapshot;
struct WalletUpdate;
} // namespace wallet_engine

namespace Main {
class Account;
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
class VaultRuntime;

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
	Locked,
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
	// Always a magnitude. The mappings fold a source's signed amount into
	// this field and `incoming`, because every surface that paints a
	// transfer prefixes a direction character of its own and would
	// otherwise render two signs.
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

enum class PhraseMatch {
	Rotation,
	Foreign,
	None,
};

[[nodiscard]] PhraseMatch DetectPhraseMatch(const std::vector<QString> &words);

// What logging out of an account, or removing its keys after a forgotten
// passcode, would destroy on this device, read from its custody store:
// `unbacked` counts the served wallet when Telegram holds no backup of it,
// `parked` counts every record the server no longer serves, `rotating` counts
// a key change this device started and nothing has confirmed - its
// replacement key is named by no record yet, so a promotion is what would put
// it under a record, and no backup can cover it before that promotion, which
// is why it is neither of the first two - and `unknown` says the account's
// loss cannot be stated as a fact, because the custody store could not be read
// or the served wallet's state has not reached this client. `holdsRecords` is
// not a loss: it says whether the store held any record at all, which is what
// tells an empty loss "every record is backed" apart from "there is nothing
// here to lose", and carrying it is what lets the forgot confirmation decide
// that without reading the store a second time. Two invariants a reviewer must
// be able to check by reading the bodies: nothing here unlocks a vault or
// reads a secret, and nothing here writes.
//
// Two renderers state this model, and they are declared and defined next to
// each other below for the reason a comment could not enforce: a term given to
// one and forgotten in the other silently drops a sentence from a
// confirmation, and the one that would lose it is the irreversible one.
// add() is the third place a term has to be taught, and it is a member so that
// the whole-domain confirmation cannot sum the fields by hand again.
struct WalletLoss {
	int unbacked = 0;
	int parked = 0;
	int rotating = 0;
	bool holdsRecords = false;
	bool unknown = false;

	void add(const WalletLoss &other);
};

[[nodiscard]] WalletLoss WalletLossOnLogout(
	not_null<Main::Account*> account);
[[nodiscard]] QString WalletLossWarning(WalletLoss loss);
[[nodiscard]] QString ForgottenPasscodeLoss(WalletLoss loss);

class Session final {
public:
	explicit Session(not_null<Main::Session*> session);
	~Session();

	[[nodiscard]] Presence presence();
	[[nodiscard]] Presence presenceCurrent() const;
	[[nodiscard]] rpl::producer<Presence> presenceValue();
	[[nodiscard]] std::optional<QString> address();
	[[nodiscard]] QString addressFriendly(bool bounceable = false);
	[[nodiscard]] WalletCapabilities capabilities() const;
	[[nodiscard]] rpl::producer<WalletCapabilities> capabilitiesValue() const;
	[[nodiscard]] QByteArray publicKey() const;
	[[nodiscard]] bool revealsLocally();
	[[nodiscard]] VaultRuntime &vault();
	[[nodiscard]] DeviceCustodyState deviceCustodyState() const;
	[[nodiscard]] auto deviceCustodyStateValue() const
		-> rpl::producer<DeviceCustodyState>;
	[[nodiscard]] rpl::producer<> custodyUpdates() const;

	// Nothing else publishes a change of the vault header: custodyUpdates()
	// fires only on a settled server state, and switching the wrap touches
	// neither the custody store nor the app lock. The key protection box
	// announces a committed wrap change here, so a surface that names the
	// wrap's kind can follow it.
	[[nodiscard]] rpl::producer<> keyProtectionUpdates() const;
	void notifyKeyProtectionChanged();

	// The cached custody store and updateDeviceCustodyState() are private, so
	// nothing outside this class can make the live session follow a device
	// whose wallet keys have just been destroyed - and the device mode must
	// follow that drop. The forgot-passcode path is the only caller, and it
	// owns the storage side: it has already removed the sealed values, emptied
	// the custody store and removed the vault header before calling this,
	// which writes nothing and only makes the session agree with the disk.
	void dropCustodyAfterForgottenPasscode();

	[[nodiscard]] std::vector<CustodyRecord> parkedRecords();

	void refreshState();
	void applyUpdate(const MTPDupdateWalletState &data);

	void revealPhrase(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail);
	// Both replace shapes share one done at their single caller, so they
	// carry one callback type. Only an imported replace runs a custody
	// install, so only it can answer WriteFailed; a create replace has no
	// custody install to report and always answers Installed, the value
	// every consumer reads as the success.
	void replaceWithNew(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &error)> fail);
	void replaceWithImported(
		KeyAuthorization auth,
		std::vector<QString> words,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &error)> fail);
	void restoreFromPhrase(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void restoreFromBackup(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void revealParked(
		KeyAuthorization auth,
		const QByteArray &publicKey,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail);
	void dropParked(
		const QByteArray &publicKey,
		Fn<void()> done,
		Fn<void(const QString &error)> fail);
	void prepareBackupParts(
		KeyAuthorization auth,
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
	void quoteRotationFee(KeyAuthorization auth, Fn<void(FeeResult)> done);
	void prepareRotation(
		KeyAuthorization auth,
		int64 quotedFeeNano,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail);
	void abandonRotation();
	void submitRotation(
		KeyAuthorization auth,
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

	// One live send box owns one preview identity until its lifetime ends.
	// Edits replace only that owner's queued request, preserving its place
	// behind other owners; cancellation discards its current request without
	// retiring the owner. Current callbacks can complete synchronously with
	// a refusal, and never run after the owning lifetime is destroyed.
	[[nodiscard]] uint64 createPreviewOwner(rpl::lifetime &lifetime);
	void estimateFee(
		uint64 owner,
		const SendArgs &args,
		Fn<void(FeeResult)> done);
	void cancelFeeEstimate(uint64 owner);
	void send(
		KeyAuthorization auth,
		SendArgs args,
		Fn<void(SendError)> done);
	[[nodiscard]] SendState sendState() const;
	[[nodiscard]] rpl::producer<SendState> sendStateValue() const;
	[[nodiscard]] const std::optional<PendingSendInfo> &pendingSend() const;

private:
	void ensureLoaded();
	void requestState();
	void applyState(const MTPWalletState &state, bool pushed);
	void setPresence(Presence presence);
	void revealLocally(
		KeyAuthorization auth,
		const CustodyRecord &record,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail);
	void revealFromShares(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail);
	void fetchShareParts(
		KeyAuthorization auth,
		const QString &token,
		std::vector<int> dcs,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail);
	void restoreFromWords(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail);
	[[nodiscard]] bool custodyBusy() const;
	[[nodiscard]] const CustodyStore &custody();
	[[nodiscard]] bool persistCustody(const CustodyRecord &record);
	void dropCreatedVault();
	void sendReplaceWallet(
		const MTPInputWalletReplacement &wallet,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const QString &)> fail);
	void finishConfirmedReplace(
		std::optional<CustodyRecord> oldRecord,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void(CustodyOutcome)> done,
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
	void refreshStaleHistory();
	void clearHistory();
	void clearCollectibles();
	void refreshCollectibles(bool force = false);
	void requestCollectibles(bool more);
	void applyCollectiblesUpdate(
		const wallet_engine::WalletUpdate &update,
		bool more);
	void setCollectibles(std::vector<Gram::NftItem> &&list);
	struct PreviewRequest;
	struct PreviewState;
	[[nodiscard]] bool previewCurrent(const PreviewRequest &request) const;
	[[nodiscard]] SendError previewError(const PreviewRequest &request);
	void startPreview();
	void finishPreview(uint64 flight, FeeResult result);
	void cancelPreview();
	void finishPreviewCancel(uint64 flight);
	void settlePreview();
	void retirePreviewOwner(uint64 owner);
	void retirePreviews(SendError error);
	void resolvePending();
	void updateListsGate();
	[[nodiscard]] bool listsConfirmedEmpty() const;
	void finishPending();
	void applyRotationSnapshot(
		const wallet_engine::SendSnapshot &snapshot,
		bool journalAuthoritative);
	void storePendingRotation(
		KeyAuthorization auth,
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
	rpl::event_stream<> _keyProtectionUpdates;
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
	bool _historyStale = false;
	crl::time _historyRequestedAt = 0;
	QString _historyNextOffset;
	std::vector<Fn<void()>> _historyDone;

	rpl::variable<SendState> _sendState = SendState::Idle;
	std::optional<PendingSendInfo> _pending;
	bool _sendUnresolved = false;
	bool _previewPending = false;
	std::unique_ptr<PreviewState> _preview;

	struct PreparedRotation;
	std::unique_ptr<PreparedRotation> _preparedRotation;
	bool _rotating = false;
	Fn<void()> _rotationConfirmed;
	Fn<void(const QString &)> _rotationFailed;

	std::unique_ptr<Ui::SeparatePanel> _panel;

};

} // namespace Wallet
