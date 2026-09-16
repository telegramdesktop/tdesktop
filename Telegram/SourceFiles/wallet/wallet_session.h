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
#include "data/data_peer_id.h"
#include "gram/api/gram_api_nft.h"
#include "mtproto/sender.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_stream.h"
#include "wallet/wallet_transfer_store.h"
#include "wallet/wallet_unlock.h"

namespace wallet_engine {
struct ActivityItem;
struct NftList;
struct SendMessageBody;
struct SendSnapshot;
struct WalletClient;
struct WalletDescriptor;
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
class Session;
struct ShareFetch;
struct TransferSubmissionAnswer;
struct TransferSubmissionData;
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
	AmountTooSmall,
	CommentTooLong,
	CommentEncryptionUnavailable,
	InsufficientBalance,
	InsufficientFees,
	PreviousUnresolved,
	AlreadySending,
	SigningUnavailable,
	Locked,
	Failed,
	Rejected,
	DataInvalid,
	QuoteExpired,
	LinkExpired,
	Silent,
	// The send callback's third outcome beside None and a refusal:
	// the signed message is journaled and may already be on the
	// network, so nothing was refused and nothing is known to have
	// been delivered. The engine's resolver settles it later; a
	// surface presents it as pending, never as sent, never as failed.
	SubmissionUnknown,
};

enum class SendState {
	Idle,
	Sending,
	Pending,
};

struct TransferWalletIdentity {
	QString address;
	QByteArray publicKey;
	uint64 revision = 0;

	friend bool operator==(
		const TransferWalletIdentity &,
		const TransferWalletIdentity &) = default;
};

struct BackupDisableApproval {
	QString address;
	QString recordId;
	int networkGeneration = 0;
};

struct TransferItem {
	enum class Source {
		Unknown,
		Server,
		Engine,
	};
	enum class EncryptedFormat {
		Unavailable,
		ServerPayload,
		EngineBodyBoc,
	};
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

	Source source = Source::Unknown;
	QString id;
	std::optional<TransferWalletIdentity> walletIdentity;
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
	std::optional<int64> feeNano;
	bool gasless = false;
	QString comment;
	bool commentEncrypted = false;
	EncryptedFormat encryptedFormat = EncryptedFormat::Unavailable;
	QByteArray encryptedPayload;
	std::optional<TimeId> date;
	quint64 lt = 0;
	QByteArray traceId;
	QByteArray externalHashNorm;
	Status status = Status::Success;

	friend bool operator==(
		const TransferItem &,
		const TransferItem &) = default;
};

// The session binds a scope before prompts to one immutable transaction and
// presentation lifetime. cancel() and Session's scope checks run on main;
// the worker observes only its atomic retirement flag and the vault epoch.
// Retirement cancels owned recovery transport, while an import already in
// flight remains owned through rollback and custody-latch settlement.
class CommentScope final {
public:
	void cancel();
	[[nodiscard]] bool cancelled() const;
	[[nodiscard]] rpl::producer<> cancelledChanges() const;

private:
	friend class Session;
	struct State;
	explicit CommentScope(std::shared_ptr<State> state);

	const std::shared_ptr<State> _state;
	rpl::event_stream<> _cancelledChanges;

};

enum class CommentDecryptError {
	None,
	Cancelled,
	Unavailable,
	Locked,
	DecryptionFailed,
	Failed,
};

struct CommentDecryptResult {
	QString text;
	CommentDecryptError error = CommentDecryptError::None;
};

inline constexpr auto kTransferMinNanosDefault = int64(100'000'000);
inline constexpr auto kTransferMinNanosMax = (int64(1) << 53);

inline constexpr auto kGaslessMinNanosDefault = int64(100'000'000);

struct GaslessInfo {
	QString relayer;
	int64 minAmount = 0;
	TimeId resetAt = 0;
	int left = 0;
	bool available = false;

	friend bool operator==(const GaslessInfo &, const GaslessInfo &) = default;
};

struct GaslessTerms {
	std::optional<TransferWalletIdentity> identity;
	std::optional<GaslessInfo> info;
	int64 transferMinNanos = kTransferMinNanosDefault;
	int64 configuredMinNanos = kGaslessMinNanosDefault;
	int64 effectiveMinNanos = kGaslessMinNanosDefault;
	uint64 revision = 0;
	bool fresh = false;
	bool usable = false;

	[[nodiscard]] bool eligible(int64 amountNano) const;
	[[nodiscard]] bool eligible(
		int64 amountNano,
		const QString &destination) const;

	friend bool operator==(const GaslessTerms &, const GaslessTerms &) = default;
};

struct PreparedSend;

struct FeeResult {
	int64 feeNano = 0;
	SendError error = SendError::None;
	std::shared_ptr<const PreparedSend> prepared;
};

struct PendingSendInfo {
	std::string operationId;
	TransferWalletIdentity walletIdentity;
	TimeId posted = 0;
	int64 amountNano = 0;
	QString destination;
	QString comment;
};

struct TransferReceipt {
	QByteArray messageHash;
	bool gasless = false;
};

struct SendComment {
	QString text;
	bool isPublic = false;

	friend bool operator==(const SendComment &, const SendComment &) = default;
};

[[nodiscard]] int SendCommentBytes(const QString &text);
[[nodiscard]] bool SendCommentFits(const QString &text);

[[nodiscard]] int64 TransferMinNanosFromConfig(float64 configured);
[[nodiscard]] int64 TransferMinNanos(not_null<Main::Session*> session);
[[nodiscard]] bool TransferAmountBelowMinimum(
	int64 amountNano,
	int64 minNanos);

// One policy read from two sides. A send judges the positive amount the
// user asked to transfer, so its entry keeps a `> 0` guard of its own; a
// history row judges a magnitude, because a transfer's sign is only its
// direction and the stored amount has already been folded to a positive
// one. Both sides ask the same comparison, and it is stated once, in the
// magnitude predicate the other two are written in terms of.
[[nodiscard]] bool TransferMagnitudeBelowMinimum(
	int64 amountNano,
	int64 minNanos);
[[nodiscard]] bool HistoryTransferHidden(
	const TransferItem &item,
	int64 minNanos);

struct SendArgs {
	QString destination;
	int64 amountNano = 0;
	UserId userId;
	SendComment comment;
	// The recipient's Ed25519 public key, when Telegram named one for this
	// destination. A private comment encrypts for it without the recipient's
	// `get_public_key`, which a wallet that was never deployed cannot answer.
	QByteArray recipientPublicKey;
	bool bounce = true;

	friend bool operator==(const SendArgs &, const SendArgs &) = default;
};

[[nodiscard]] QByteArray DecodeServerEncryptedComment(const QString &encoded);

[[nodiscard]] std::vector<TransferItem> HistoryFromEngine(
	const std::vector<wallet_engine::ActivityItem> &items,
	std::optional<TransferWalletIdentity> identity = std::nullopt);

[[nodiscard]] std::vector<TransferItem> HistoryFromServer(
	const QVector<MTPWalletTransaction> &list,
	std::optional<TransferWalletIdentity> identity = std::nullopt);

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

struct PhraseIdentity {
	QByteArray anchor;
	QByteArray signing;
};

// What logging out of an account, or removing its keys after a forgotten
// passcode, would destroy on this device, read from its custody store:
// `unbacked` counts the served wallet when Telegram holds no backup of it,
// `parked` counts every record of another wallet, `rotating` counts
// a key change this device started and nothing has confirmed - its
// replacement key is named by no record yet, so a promotion is what would put
// it under a record, and no backup can cover it before that promotion, which
// is why it is neither of the first two - and `unknown` says the account's
// loss cannot be stated as a fact, because the custody store could not be read
// or the served wallet's state has not reached this client. A record of the
// served wallet that holds an obsolete signing key is no loss: the key no
// longer signs, so nothing it could reveal is current. A pre-v4 record of the
// served wallet, whose signing key was never established, counts as active,
// because it may be the only copy of the current phrase. `holdsRecords` is
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
	// The served wallet's current on-chain signing key, not the anchor.
	[[nodiscard]] QByteArray publicKey() const;
	[[nodiscard]] auto transferWalletIdentity() const
		-> std::optional<TransferWalletIdentity>;
	[[nodiscard]] bool transferWalletIdentityCurrent(
		const TransferWalletIdentity &identity) const;
	[[nodiscard]] rpl::producer<> transferWalletIdentityChanges() const;
	[[nodiscard]] bool revealsLocally();
	[[nodiscard]] std::optional<BackupDisableApproval> backupDisableApproval();
	[[nodiscard]] VaultRuntime &vault() const;
	[[nodiscard]] DeviceCustodyState deviceCustodyState() const;
	[[nodiscard]] auto deviceCustodyStateValue() const
		-> rpl::producer<DeviceCustodyState>;
	[[nodiscard]] rpl::producer<> custodyUpdates() const;
	[[nodiscard]] bool custodyBusy() const;

	// Protection and unusable state belong to the whole domain. Every session
	// observes the same event and updates only its already-cached device mode.
	// Announcing a change neither opens D nor reads or writes custody, and
	// never reconciles a passcode while an install is only armed. A failed
	// reset can preserve unusable state when it announces its terminal result.
	[[nodiscard]] rpl::producer<> keyProtectionUpdates() const;
	void notifyKeyProtectionChanged(bool vaultKeyStillUnusable = false);

	void resetCustodyAfterForgottenPasscode(Fn<void(CustodyResetResult)> done);

	// The shared runtime marks all accounts read-only when the current live
	// factor is Absent, Unavailable or Corrupt. This state never edits disk;
	// the existing confirmed reset owns deletion. An authentication failure
	// or cancellation does not set it, and a stale factor with no live entry
	// cannot prevent a fresh install from choosing its own protection.
	[[nodiscard]] bool vaultKeyUnusable() const;
	void setVaultKeyUnusable(bool unusable);

	[[nodiscard]] std::vector<CustodyRecord> parkedRecords();

	void refreshState();
	void applyUpdate(const MTPDupdateWalletState &data);
	void applyUpdate(const MTPDupdateSentWalletTransaction &data);
	void applyUpdate(const MTPDupdateWalletGaslessInfo &data);

	[[nodiscard]] std::shared_ptr<CommentScope> createCommentScope(
		TransferItem target,
		rpl::lifetime &lifetime);
	[[nodiscard]] bool commentScopeCurrent(
		const std::shared_ptr<CommentScope> &scope) const;
	void decryptComment(
		KeyAuthorization auth,
		std::shared_ptr<CommentScope> scope,
		Fn<void(CommentDecryptResult)> done);

	void revealPhrase(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail,
		Fn<void()> authorized = nullptr);
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
	void restoreFromPhrase(
		KeyAuthorization auth,
		std::vector<QString> words,
		std::shared_ptr<CommentScope> scope,
		Fn<void(KeyAuthorization)> done,
		Fn<void(const QString &error)> fail);
	void restoreFromBackup(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		std::shared_ptr<CommentScope> scope,
		Fn<void(KeyAuthorization)> done,
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
	void disableBackupWithProof(
		KeyAuthorization auth,
		BackupDisableApproval approved,
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
	[[nodiscard]] int64 transferMinNanos() const;
	[[nodiscard]] bool historyItemHidden(const TransferItem &item) const;
	[[nodiscard]] bool historyVisibleEmpty() const;
	[[nodiscard]] bool listsGated() const;
	[[nodiscard]] rpl::producer<bool> listsGatedValue() const;
	[[nodiscard]] rpl::producer<ListsEmptyState> listsEmptyStateValue() const;

	void refreshHistory(Fn<void()> done = nullptr);
	[[nodiscard]] bool historyHasNext() const;
	[[nodiscard]] bool historyLoadingMore() const;
	[[nodiscard]] rpl::producer<bool> historyLoadingMoreValue() const;
	void loadMoreHistory();
	void resetHiddenHistoryPages();

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

	[[nodiscard]] GaslessTerms gaslessTerms();
	[[nodiscard]] rpl::producer<GaslessTerms> gaslessTermsValue();
	void refreshGaslessInfo(bool force = false);

	[[nodiscard]] rpl::producer<bool> existingWaltBalanceValue();

	// One live send box owns one preview identity until its lifetime ends.
	// Edits replace only that owner's queued request, preserving its place
	// behind other owners; cancellation discards its current request without
	// retiring the owner. Current callbacks can complete synchronously with
	// a refusal, and never run after the owning lifetime is destroyed.
	[[nodiscard]] uint64 createPreviewOwner(rpl::lifetime &lifetime);
	void estimateFee(
		KeyAuthorization auth,
		uint64 owner,
		const SendArgs &args,
		Fn<void(FeeResult)> done);
	void cancelFeeEstimate(uint64 owner);
	void send(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done);
	[[nodiscard]] SendState sendState() const;
	[[nodiscard]] rpl::producer<SendState> sendStateValue() const;
	[[nodiscard]] std::optional<PendingSendInfo> pendingSend() const;
	[[nodiscard]] auto lastTransferReceipt() const
		-> const std::optional<TransferReceipt> &;
	[[nodiscard]] std::vector<TransferItem> submittedTransactions() const;
	[[nodiscard]] std::optional<TransferItem> submittedTransaction(
		const std::string &operationId) const;

private:
	void ensureLoaded();
	void requestState(
		Fn<void(const MTPWalletState &)> done = nullptr,
		Fn<void()> fail = nullptr);
	void requestGaslessInfo();
	void retireGaslessRequest();
	void resetGaslessInfo();
	void applyGaslessInfo(GaslessInfo info, bool refreshed);
	void applyGaslessTerms(GaslessTerms terms);
	void requestExistingWaltBalance();
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
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope = nullptr,
		Fn<void()> authorized = nullptr);
	void fetchShareParts(
		KeyAuthorization auth,
		const QString &token,
		std::vector<int> dcs,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope = nullptr);
	void restoreFromWords(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope = nullptr);
	// The phrase's anchor and current signing public keys, both derived
	// storage- and network-free on the engine's local worker; nullopt for a
	// phrase that is not a valid Rotation mnemonic. done runs on the main
	// thread.
	void validatePhraseIdentity(
		const std::vector<QString> &words,
		Fn<void(std::optional<PhraseIdentity>)> done);
	[[nodiscard]] bool commentAccessAvailable() const;
	void validateCommentScopes();
	void retireCommentScopes(
		const std::shared_ptr<CommentScope> &except = nullptr);
	[[nodiscard]] const CustodyStore &custody();
	[[nodiscard]] const CustodyRecord *currentRecord();
	[[nodiscard]] bool persistCustody(const CustodyRecord &record);
	void resetUnusableVault(
		const QByteArray &expected,
		const std::shared_ptr<CommentScope> &scope,
		Fn<void(CustodyResetResult)> done);
	void resetDeviceCustody(
		const std::shared_ptr<CommentScope> &scope,
		Fn<bool()> current,
		Fn<void(CustodyResetResult)> done);
	[[nodiscard]] CustodyInstallRequest resettableInstallRequest(
		QByteArray expected,
		std::shared_ptr<CommentScope> scope,
		std::shared_ptr<bool> crossed,
		Fn<void(CustodyInstall)> proceed);
	void settleVaultReset(
		const std::shared_ptr<bool> &crossed,
		bool installed);
	void sendReplaceWallet(
		const MTPInputWalletReplacement &wallet,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const MTP::Error &)> fail);
	void recoverImportedReplace(
		QString canonicalAddress,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const QString &)> abandon);
	struct OwnershipProof {
		TimeId timestamp = 0;
		std::vector<uint8_t> signature;
	};
	enum class OwnershipProofError {
		Failed,
		VaultLocked,
	};
	void requestOwnershipProof(
		wallet_engine::WalletDescriptor descriptor,
		QByteArray signingKey,
		VaultAuthorization grant,
		Fn<void(OwnershipProof)> done,
		Fn<void(OwnershipProofError)> fail);
	void settleRefusedBackupDisable(
		QString address,
		QByteArray proofKey,
		QString error,
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	void finishConfirmedReplace(
		QString oldAddress,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &)> fail);
	void reconcileCustody();
	void updateDeviceCustodyState(bool cachedOnly = false);
	void syncEngineClient();
	void stopEngineClientForReset(Fn<void()> done);
	void removeCustodyRecord(const QString &recordId);
	[[nodiscard]] bool parked(const CustodyRecord &record) const;
	void establishSigningKey(
		const QString &recordId,
		const PhraseIdentity &identity);
	void clearNetworkState();
	void pollTick();
	void updatePollingState();
	void applyStreamRefresh(StreamRefresh wanted);
	void requestEngineRefresh();
	void applyEngineUpdate(
		const wallet_engine::WalletUpdate &update,
		uint64 sendRevision);
	void setHistory(std::vector<TransferItem> &&list);
	struct HistoryRequest;
	void requestTransactions(bool more, Fn<void()> done = nullptr);
	[[nodiscard]] bool historyRequestCurrent(
		const HistoryRequest &request) const;
	void applyTransactions(
		const MTPwallet_Transactions &result,
		bool more,
		const HistoryRequest &request);
	void refreshStaleHistory();
	void applyTransferMinNanos();
	void continueHiddenHistory(bool progressed);
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
	[[nodiscard]] bool transferClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<wallet_engine::WalletClient> &client) const;
	[[nodiscard]] SendError previewError(const PreviewRequest &request);
	void startPreview();
	void previewPrepared(uint64 flight, wallet_engine::SendMessageBody body);
	void finishPreview(uint64 flight, FeeResult result);
	void cancelPreview();
	void finishPreviewCancel(uint64 flight);
	void settlePreview();
	void retirePreviewOwner(uint64 owner);
	void retirePreviews(SendError error);
	[[nodiscard]] bool sendRecoveryNeeded() const;
	void restoreSubmittedTransfers();
	void resolvePending();
	void updateListsGate();
	[[nodiscard]] bool listsConfirmedEmpty() const;
	void finishPending();
	void applySendSnapshot(
		const wallet_engine::SendSnapshot &snapshot,
		bool journalAuthoritative,
		uint64 sendRevision);
	[[nodiscard]] bool submissionCurrent(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared) const;
	void submitTransfer(
		std::string operationId,
		std::shared_ptr<const PreparedSend> prepared,
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done);
	[[nodiscard]] bool bindTransferReceipt(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared,
		const MTPDupdateSentWalletTransaction &data);
	[[nodiscard]] bool applySubmittedUpdate(
		const std::string &operationId,
		const MTPDupdateSentWalletTransaction &data);
	[[nodiscard]] bool adoptSubmittedTransaction(
		const std::string &operationId,
		TransferItem item);
	[[nodiscard]] bool transferOperationCurrent(
		const TransferWalletIdentity &identity,
		int generation,
		const std::shared_ptr<wallet_engine::WalletClient> &client) const;
	struct SubmittedTransfer;
	struct SubmittedLookup;
	[[nodiscard]] SubmittedTransferStore &submittedTransferStore();
	[[nodiscard]] SubmittedTransferRecord *submittedTransferRecord(
		const std::string &operationId,
		const TransferWalletIdentity &identity);
	[[nodiscard]] bool persistSubmittedTransfers();
	void retireSubmittedTransferRecord(
		const std::string &operationId,
		const TransferWalletIdentity &identity);
	[[nodiscard]] SubmittedTransfer *upsertSubmittedTransfer(
		const std::string &operationId,
		const TransferWalletIdentity &identity,
		int generation,
		const std::shared_ptr<wallet_engine::WalletClient> &client);
	[[nodiscard]] SubmittedTransfer *submittedTransfer(
		const std::string &operationId);
	[[nodiscard]] bool submittedLookupNeeded() const;
	[[nodiscard]] bool submittedLookupCurrent(
		const std::shared_ptr<SubmittedLookup> &request) const;
	void startSubmittedLookup();
	void lookupSubmittedTransaction();
	void applySubmittedLookup(
		const MTPwallet_Transactions &result,
		const std::shared_ptr<SubmittedLookup> &request);
	void dropSubmittedIfListed();
	void dropSubmittedLookup();
	void clearSubmittedTransfers();
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
	base::Timer _gaslessTimer;
	std::weak_ptr<ShareFetch> _shareFetch;

	bool _loaded = false;
	QString _address;
	QByteArray _publicKey;
	uint64 _walletIdentityRevision = 0;
	rpl::event_stream<> _transferWalletIdentityChanges;

	rpl::variable<int64> _balanceNano = 0;
	rpl::variable<Presence> _presence = Presence::Unknown;
	rpl::variable<WalletCapabilities> _capabilities;
	std::optional<CustodyStore> _custody;
	bool _custodyReadFailed = false;
	bool _custodyResetting = false;
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
	int64 _transferMinNanos = kTransferMinNanosDefault;
	rpl::variable<GaslessTerms> _gaslessTerms;
	mtpRequestId _gaslessRequestId = 0;
	uint64 _gaslessRequestSerial = 0;
	crl::time _gaslessRequestedAt = 0;
	crl::time _gaslessExpiresAt = 0;
	bool _gaslessRefreshWanted = false;
	bool _gaslessRefreshing = false;
	rpl::variable<bool> _existingWaltBalance = false;
	mtpRequestId _waltBalanceRequestId = 0;
	bool _waltBalanceRequested = false;
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
	std::shared_ptr<HistoryRequest> _historyRequest;
	int _historyHiddenPages = 0;
	bool _resolveRequestPending = false;
	bool _sendRecoveryReady = false;
	bool _engineRefreshPending = false;
	bool _historySettled = false;
	bool _historyUnreachable = false;
	bool _historyPaged = false;
	bool _historyStale = false;
	crl::time _historyRequestedAt = 0;
	QString _historyNextOffset;

	rpl::variable<SendState> _sendState = SendState::Idle;
	std::optional<PendingSendInfo> _pending;
	bool _sendUnresolved = false;
	std::string _unresolvedOperationId;
	uint64 _sendRevision = 0;
	struct TransferSubmissionState {
		std::string operationId;
		std::shared_ptr<const PreparedSend> prepared;
		std::optional<TransferReceipt> receipt;
		std::optional<SendError> refusal;
		bool paired = false;
		bool normalFeeAuthorized = false;
		bool rpcStarted = false;
	};
	std::optional<TransferSubmissionState> _submission;
	std::optional<SubmittedTransferStore> _submittedTransferStore;
	bool _submittedTransfersDirty = false;
	std::vector<SubmittedTransfer> _submitted;
	std::shared_ptr<SubmittedLookup> _lookup;
	std::string _lastLookupOperationId;
	std::optional<TransferReceipt> _lastReceipt;
	bool _previewPending = false;
	std::unique_ptr<PreviewState> _preview;

	struct PreparedRotation;
	std::unique_ptr<PreparedRotation> _preparedRotation;
	bool _rotating = false;
	Fn<void()> _rotationConfirmed;
	Fn<void(const QString &)> _rotationFailed;

	std::vector<std::weak_ptr<CommentScope>> _commentScopes;
	rpl::lifetime _commentLifetime;

	std::unique_ptr<Ui::SeparatePanel> _panel;

	rpl::lifetime _lifetime;

};

} // namespace Wallet
