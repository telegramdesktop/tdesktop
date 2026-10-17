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
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_nft.h"
#include "mtproto/sender.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_stream.h"
#include "wallet/wallet_transfer_store.h"
#include "wallet/wallet_unlock.h"

namespace wallet_engine {
struct ActivityItem;
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
class TonConnect;
enum class TonConnectAccess : uchar;
struct TonConnectAppRequest;
struct TonConnectEmulation;
struct TonConnectEventRequest;
struct TonConnectKey;
enum class TonConnectKeyError : uchar;
struct TonConnectReply;
struct TonConnectResponse;
struct TonConnectSignData;
struct TonConnectTransfer;
class TransferMessages;
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
	KeyMismatch,
	KeyChanged,
	QuoteExpired,
	LinkExpired,
	CollectibleUnavailable,
	CollectibleRejected,
	Silent,
	// The send callback's third outcome beside None and a refusal:
	// the signed message is journaled and may already be on the
	// network, so nothing was refused and nothing is known to have
	// been delivered. The engine's resolver settles it later; a
	// surface presents it as pending, never as sent, never as failed.
	SubmissionUnknown,
};

enum class CommentRecipient {
	Encryptable,
	PlainOnly,
	Unknown,
};

enum class DnsLookupError {
	Busy,
	Failed,
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
		Collectible,
		Onramp,
		PeerTransfer,
		KeyChange,
	};

	Source source = Source::Unknown;
	QString id;
	std::optional<TransferWalletIdentity> walletIdentity;
	Kind kind = Kind::Transfer;
	bool incoming = false;
	QString counterparty;
	bool counterpartyBounceable = false; // as served or as sent, raw is UQ
	QString counterpartyName;
	quint64 counterpartyPeer = 0;
	QString collectible;
	std::optional<Gram::NftItem> collectibleRecord;
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
	QString failureReason;
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
	// The host could not read the stored key at all.
	KeyUnreadable,
	// The key was read and does not open this comment.
	DecryptionFailed,
	// The engine's one resolution slot was taken. The session retries a
	// bounded number of times and never surfaces this value itself.
	Busy,
	Failed,
};

struct CommentDecryptResult {
	QString text;
	CommentDecryptError error = CommentDecryptError::None;
};

// The answer to one transaction lookup. An empty |item| with |failed| false
// is the server not naming that transaction yet, which is a transfer whose
// message arrived before it was indexed, and not the same as a refusal.
struct ResolvedTransaction {
	std::optional<TransferItem> item;
	bool failed = false;
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
	// Null for a private comment priced without the key: a fee, no transfer.
	std::shared_ptr<const PreparedSend> prepared;
	std::shared_ptr<const TonConnectEmulation> emulation;
};

struct PendingSendInfo {
	std::string operationId;
	TransferWalletIdentity walletIdentity;
	TimeId posted = 0;
	int64 amountNano = 0;
	QString destination;
	QString collectible;
	QString comment;
	UserId recipient;
	bool bounce = false;
};

[[nodiscard]] TransferItem ItemFromPending(const PendingSendInfo &pending);

struct SendStarted {
	std::string operationId;
	FullMsgId message;
};

struct ListedSubmittedTransfer {
	std::string operationId;
	TransferItem item;
};

struct TransferReceipt {
	QByteArray messageHash;
	bool gasless = false;
};

struct TonConnectSendResult {
	QString signedBoc;
	SendError error = SendError::None;
};

struct TonConnectSendLink {
	std::string operationId;
	Fn<bool(const QString &signedBoc)> handoff;
};

enum class TonConnectSendFate : uchar {
	Unknown,
	Absent,
	Unresolved,
	NotExecuted,
	Settled,
	Sending,
};

struct SendComment {
	QString text;
	bool isPublic = false;

	friend bool operator==(const SendComment &, const SendComment &) = default;
};

[[nodiscard]] int SendCommentBytes(const QString &text);
[[nodiscard]] bool SendCommentFits(const QString &text);
[[nodiscard]] int64 CollectibleTransferAttachedNanos();

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
	// An owned collectible's item address; the session picks amountNano then.
	QString collectible;
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

// The three states a private comment is laid out in, all decided by the
// message bytes alone, so a surface settles them while it builds the
// comment instead of offering a reveal that no key could ever serve.
// Pending is this device's own transfer before the server named it: it
// carries the text the user typed and no payload, because only the served
// transaction carries one. Unusable is a payload that cannot be decrypted
// by anything - the sender address does not parse, the transaction is
// named by nothing, or the body fails its structural parse. Revealable is
// the rest, and the only one a spoiler belongs on.
[[nodiscard]] bool EncryptedCommentPending(const TransferItem &item);
[[nodiscard]] bool EncryptedCommentUnusable(const TransferItem &item);
[[nodiscard]] bool EncryptedCommentRevealable(const TransferItem &item);

// The 32 hash bytes in base64 or hex, or nothing for another shape.
[[nodiscard]] QByteArray TransactionHashFromServer(const QString &value);

[[nodiscard]] std::vector<TransferItem> HistoryFromEngine(
	const std::vector<wallet_engine::ActivityItem> &items,
	std::optional<TransferWalletIdentity> identity = std::nullopt);

[[nodiscard]] std::vector<TransferItem> HistoryFromServer(
	const QVector<MTPWalletTransaction> &list,
	std::optional<TransferWalletIdentity> identity = std::nullopt);

[[nodiscard]] std::vector<Gram::NftItem> CollectiblesFromServer(
	const QVector<MTPwallet_NftItem> &list);

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
	// A record-bound engine client that has finished recovering its send
	// journal: the state send() requires beyond a valid authorization. A
	// public-key-only client previews fees but never counts as ready.
	[[nodiscard]] bool signingReady() const;
	[[nodiscard]] rpl::producer<bool> signingReadyValue() const;
	[[nodiscard]] bool revealsLocally();
	[[nodiscard]] std::optional<BackupDisableApproval> backupDisableApproval();
	[[nodiscard]] VaultRuntime &vault() const;
	[[nodiscard]] DeviceCustodyState deviceCustodyState() const;
	[[nodiscard]] auto deviceCustodyStateValue() const
		-> rpl::producer<DeviceCustodyState>;
	[[nodiscard]] rpl::producer<> custodyUpdates() const;
	[[nodiscard]] bool custodyBusy() const;
	// Everything createCommentScope() asks of the wallet, with nothing about
	// the message it would be opened over. A surface waiting for a comment
	// to become openable waits on this, so it waits for the same state the
	// scope will be made in instead of one the next call disproves.
	[[nodiscard]] bool commentAccessReady() const;

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

	void ensureLoaded();
	void refreshState();

	// Asks Telegram for one transaction by the id a message carried, for a
	// surface that has only what that message said about it. |done| runs
	// exactly once. A transaction the server does not name yet answers with
	// an empty |item| and |failed| false, which is what a just-sent transfer
	// looks like until it is indexed, so a caller can ask again; a request
	// that failed answers |failed| true. Destruction retires the request
	// without running |done|.
	void resolveTransaction(
		const QString &id,
		Fn<void(ResolvedTransaction)> done);

	void applyUpdate(const MTPDupdateWalletState &data);
	void applyUpdate(const MTPDupdateSentWalletTransaction &data);
	void applyUpdate(const MTPDupdateWalletGaslessInfo &data);
	void applyUpdate(const MTPDupdateWalletTonConnectSession &data);
	void applyUpdate(const MTPDupdateWalletTonConnectPendingDisconnect &data);

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
	void enableBackup(
		KeyAuthorization auth,
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
	[[nodiscard]] TonConnectAccess tonConnectAccess();
	[[nodiscard]] bool tonConnectProofDomainAllowed(
		const QString &domain) const;
	void deriveTonConnectSession(
		KeyAuthorization auth,
		const QString &dappClientId,
		const QByteArray &nonce,
		Fn<void(TonConnectKey)> done,
		Fn<void(TonConnectKeyError)> fail);
	void prepareTonConnectEvent(
		KeyAuthorization auth,
		TonConnectKey key,
		TonConnectEventRequest request,
		Fn<void(TonConnectReply)> done,
		Fn<void(TonConnectKeyError)> fail);
	void prepareTonConnectError(
		TonConnectKey key,
		QByteArray challenge,
		uint64 eventId,
		int code,
		Fn<void(TonConnectReply)> done,
		Fn<void()> fail);
	void prepareTonConnectDisconnect(
		TonConnectKey key,
		uint64 eventId,
		Fn<void(QByteArray)> done,
		Fn<void()> fail);
	void decryptTonConnectRequest(
		TonConnectKey key,
		QByteArray body,
		Fn<void(TonConnectAppRequest)> done,
		Fn<void()> fail);
	void answerTonConnectChallenge(
		TonConnectKey key,
		QByteArray challenge,
		Fn<void(QByteArray)> done,
		Fn<void()> fail);
	void encryptTonConnectResponse(
		TonConnectKey key,
		QString requestId,
		TonConnectResponse response,
		Fn<void(QByteArray)> done,
		Fn<void()> fail);
	void signTonConnectData(
		KeyAuthorization auth,
		TonConnectKey key,
		QString requestId,
		std::shared_ptr<const TonConnectSignData> data,
		QString domain,
		Fn<void(QByteArray)> done,
		Fn<void(TonConnectKeyError)> fail);
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
	[[nodiscard]] bool historyCanPage() const;
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
	[[nodiscard]] TransferMessages &transferMessages();
	[[nodiscard]] UserAddresses &userAddresses();
	[[nodiscard]] TonConnect &tonConnect();

	[[nodiscard]] Ui::SeparatePanel *panel() const;
	void setPanel(std::unique_ptr<Ui::SeparatePanel> panel);

	[[nodiscard]] GaslessTerms gaslessTerms();
	[[nodiscard]] rpl::producer<GaslessTerms> gaslessTermsValue();
	void refreshGaslessInfo(bool force = false);

	// Empty while there is no Walt balance to show, otherwise its link.
	[[nodiscard]] QString existingWaltBalanceUrl() const;
	[[nodiscard]] rpl::producer<QString> existingWaltBalanceUrlValue();
	[[nodiscard]] rpl::producer<std::optional<int64>> parkedBalanceNanoValue();

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
	// Asks no key; Unknown is a provider that never answered, not a refusal.
	void resolveCommentRecipient(
		const QString &destination,
		bool bounce,
		const QByteArray &recipientPublicKey,
		Fn<void(CommentRecipient)> done);
	// Asks no key; |done| gets nullopt when the name has no wallet.
	void resolveDnsName(
		const QString &name,
		Fn<void(std::optional<QString>)> done,
		Fn<void(DnsLookupError)> fail);
	void estimateTonConnect(
		uint64 owner,
		std::shared_ptr<const TonConnectTransfer> transfer,
		Fn<void(FeeResult)> done);
	// |started| fires as the transfer leaves, with its chat message if any.
	void send(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done,
		Fn<void(SendStarted)> started = nullptr);
	[[nodiscard]] SendError sendRefusal(
		const std::shared_ptr<const PreparedSend> &prepared,
		const KeyAuthorization &auth);
	void sendTonConnect(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		TonConnectSendLink link,
		Fn<void(TonConnectSendResult)> done,
		Fn<void(SendError)> settled = nullptr);
	[[nodiscard]] SendState sendState() const;
	[[nodiscard]] rpl::producer<SendState> sendStateValue() const;
	[[nodiscard]] std::optional<PendingSendInfo> pendingSend() const;
	[[nodiscard]] auto lastTransferReceipt() const
		-> const std::optional<TransferReceipt> &;
	[[nodiscard]] std::vector<TransferItem> submittedTransactions() const;
	[[nodiscard]] auto listedSubmittedTransactions() const
	-> std::vector<ListedSubmittedTransfer>;
	[[nodiscard]] std::optional<TransferItem> submittedTransaction(
		const std::string &operationId) const;
	// Includes the fallback record, wherever the history pages end.
	[[nodiscard]] std::optional<TransferItem> trackedTransaction(
		const std::string &operationId) const;
	[[nodiscard]] std::optional<TransferItem> sendingTransaction(
		const std::string &operationId) const;
	// The send the wallet window started, which its list shows as sending.
	void setWindowSend(std::string operationId);
	[[nodiscard]] const std::string &windowSend() const;
	[[nodiscard]] TonConnectSendFate tonConnectSendFate(
		const std::string &operationId);

private:
	void requestState(
		Fn<void(const MTPWalletState &)> done = nullptr,
		Fn<void()> fail = nullptr);
	void requestGaslessInfo();
	void retireGaslessRequest();
	void resetGaslessInfo();
	void applyGaslessInfo(GaslessInfo info, bool refreshed);
	void applyGaslessTerms(GaslessTerms terms);
	void requestExistingWaltBalance();
	void syncParkedChecks(
		const CustodyStore &store,
		const QString &servedAddress);
	void requestParkedFunds(const QString &address);
	void finishParkedCheck(
		const QString &address,
		uint64 revision,
		std::optional<Gram::AddressFunds> funds);
	void requestParkedChecks(bool refresh);
	void dropEmptyParked();
	void publishParkedBalance();
	[[nodiscard]] bool parkedHidden(
		const CustodyRecord &record,
		const QString &servedAddress) const;
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
		std::shared_ptr<CommentScope> scope,
		mtpRequestId exportRequestId);
	void restoreFromWords(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope = nullptr,
		mtpRequestId exportRequestId = 0);
	[[nodiscard]] QString phraseDiagnosticState() const;
	[[nodiscard]] Fn<void(const QString &)> loggedPhraseFail(
		const QString &stage,
		Fn<void(const QString &)> fail);
	// The phrase's anchor and current signing public keys, both derived
	// storage- and network-free on the engine's local worker; nullopt for a
	// phrase that is not a valid Rotation mnemonic. done runs on the main
	// thread.
	void validatePhraseIdentity(
		const std::vector<QString> &words,
		Fn<void(std::optional<PhraseIdentity>)> done);
	[[nodiscard]] bool commentAccessAvailable() const;
	// Runtime only, never written anywhere: a protected secret this device
	// holds and could not read moves the device to its read-only view, so
	// the restore and import ladder becomes reachable and a relaunch tries
	// the secret again. The signal cannot be told apart from a keyring that
	// is momentarily shut, so nothing irreversible follows from it; only a
	// secret the host states is not there at all drops its record, which is
	// the one answer that names no key any more.
	[[nodiscard]] bool secretUnreadable(const QString &recordId) const;
	void noteSecretReadFailure(
		SecretReadFailure failure,
		const QString &recordId);
	// Drops the verdict once the record it names is no longer stored.
	void validateUnreadableRecord();
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
		QByteArray signingKey,
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
	void settleRefusedBackupChange(
		QString address,
		QByteArray proofKey,
		bool backupEnabled,
		QString error,
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	[[nodiscard]] QString backupEnableRefusal(
		const QString &address,
		const QString &recordId,
		const QByteArray &proofKey,
		const QString &error);
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
	// The live client only while it is bound to a custody record, so every
	// journal, rotation and comment path that needs the secret sees no
	// client at all under a public-key-only one.
	[[nodiscard]] auto signingClient() const
		-> std::shared_ptr<wallet_engine::WalletClient>;
	void updateSigningReady();
	// Decryptions that arrived while the public-key-only client was being
	// swapped for the signing one run once that swap has settled.
	struct DeferredDecrypt {
		KeyAuthorization auth;
		std::shared_ptr<CommentScope> scope;
		Fn<void(CommentDecryptResult)> done;
		int attempts = 0;
	};
	void decryptComment(DeferredDecrypt request);
	void settleDeferredDecrypts();
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
	[[nodiscard]] bool listRequestCurrent(
		const std::optional<TransferWalletIdentity> &identity,
		uint64 identityRevision,
		int generation) const;
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
	void applyWalletAvailable();
	void continueHiddenHistory(bool progressed);
	void clearHistory();
	void clearCollectibles();
	void refreshCollectibles(bool force = false);
	void requestCollectibles(bool more);
	struct CollectiblesRequest;
	void applyCollectibles(
		const MTPwallet_NftItems &result,
		const CollectiblesRequest &request);
	void setCollectibles(std::vector<Gram::NftItem> &&list);
	void rememberCollectibles(const std::vector<TransferItem> &items);
	void followCollectibles(const std::vector<TransferItem> &arrived);
	void followCollectible(const QString &address, bool incoming);
	void spendCollectibleFollowUps(const std::vector<Gram::NftItem> *list);
	struct PreviewRequest;
	struct PreviewState;
	[[nodiscard]] bool previewCurrent(const PreviewRequest &request) const;
	[[nodiscard]] bool previewClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<wallet_engine::WalletClient> &client) const;
	[[nodiscard]] bool transferClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<wallet_engine::WalletClient> &client) const;
	[[nodiscard]] SendError previewError(const PreviewRequest &request);
	void enqueuePreview(PreviewRequest request);
	void startPreview();
	void previewPrepared(uint64 flight, wallet_engine::SendMessageBody body);
	void previewCollectible(uint64 flight);
	void finishPreview(uint64 flight, FeeResult result);
	void cancelPreview();
	void finishPreviewCancel(uint64 flight);
	void settlePreview();
	void retirePreviewOwner(uint64 owner);
	void retirePreviews(SendError error);
	void resolveCommentRecipientAttempt(
		const QString &destination,
		bool bounce,
		const QByteArray &recipientPublicKey,
		Fn<void(CommentRecipient)> done,
		int attempt);
	[[nodiscard]] bool sendRecoveryNeeded() const;
	void releaseDeferredRows();
	void restoreSubmittedTransfers();
	void dropForeignSubmittedTransfers(const TransferWalletIdentity &identity);
	void expireStaleSubmittedTransfers();
	void resolvePending();
	void updateListsGate();
	[[nodiscard]] bool listsConfirmedEmpty() const;
	void finishPending();
	void applySendSnapshot(
		const wallet_engine::SendSnapshot &snapshot,
		bool journalAuthoritative,
		uint64 sendRevision);
	void startSend(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done,
		Fn<void(SendStarted)> started,
		Fn<void(TonConnectSendResult)> tonConnect,
		TonConnectSendLink tonConnectLink);
	[[nodiscard]] bool submissionCurrent(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared) const;
	void submitTransfer(
		std::string operationId,
		std::shared_ptr<const PreparedSend> prepared,
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done);
	void settleKeyMismatch(
		TransferWalletIdentity identity,
		QByteArray signingKey,
		Fn<void(SendError)> done);
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
	[[nodiscard]] const TransferItem *submittedShown(
		const SubmittedTransfer &entry) const;
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
	void retireSubmission();
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
	const std::unique_ptr<TransferMessages> _transferMessages;
	const std::unique_ptr<TonConnect> _tonConnect;
	const std::unique_ptr<Stream> _stream;
	base::Timer _pollTimer;
	base::Timer _shareFetchTimer;
	base::Timer _gaslessTimer;
	base::Timer _decryptRetryTimer;
	std::weak_ptr<ShareFetch> _shareFetch;

	bool _loaded = false;
	bool _walletAvailable = true;
	QString _address;
	QByteArray _publicKey;
	QString _tonConnectOwnershipDomain;
	uint64 _walletIdentityRevision = 0;
	rpl::event_stream<> _transferWalletIdentityChanges;

	rpl::variable<int64> _balanceNano = 0;
	rpl::variable<Presence> _presence = Presence::Unknown;
	rpl::variable<WalletCapabilities> _capabilities;
	std::optional<CustodyStore> _custody;
	bool _custodyReadFailed = false;
	QString _unreadableRecordId;
	bool _custodyResetting = false;
	bool _phraseRevealing = false;
	bool _replacing = false;
	bool _backupChanging = false;
	rpl::variable<DeviceCustodyState> _deviceCustody;
	rpl::event_stream<> _custodyUpdates;
	QString _clientRecordId;
	std::optional<TransferWalletIdentity> _clientPreviewIdentity;
	rpl::variable<bool> _signingReady = false;
	bool _clientStopping = false;
	AccountStatus _engineStatus = AccountStatus::NonExisting;
	mtpRequestId _stateRequestId = 0;
	crl::time _stateRequestedAt = 0;
	crl::time _stateRefreshedAt = 0;
	crl::time _engineRefreshedAt = 0;
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
	rpl::variable<QString> _existingWaltBalanceUrl;
	mtpRequestId _waltBalanceRequestId = 0;
	bool _waltBalanceRequested = false;
	// Checking and Empty hide a record: an empty unused one is dropped silently.
	enum class ParkedFunds : uchar {
		Checking,
		Empty,
		Funded,
		Unknown,
	};
	struct ParkedCheck {
		ParkedFunds funds = ParkedFunds::Checking;
		int64 nano = 0;
		uint64 revision = 0;
		mtpRequestId requestId = 0;
	};
	base::flat_map<QString, ParkedCheck> _parkedChecks;
	uint64 _parkedCheckRevision = 0;
	bool _parkedDropping = false;
	rpl::variable<std::optional<int64>> _parkedBalanceNano;
	std::vector<TransferItem> _history;
	rpl::event_stream<> _historyUpdates;
	bool _historyHasNext = false;
	// Set by new rows, cleared when paging stalls: a shown row stays shown.
	std::optional<TimeId> _listedBoundary;
	crl::time _historyRefreshedAt = 0;

	std::vector<Gram::NftItem> _collectibles;
	rpl::event_stream<> _collectiblesUpdates;
	rpl::variable<bool> _collectiblesTab = false;
	crl::time _collectiblesRefreshedAt = 0;
	crl::time _collectiblesCompletedAt = 0;
	std::shared_ptr<CollectiblesRequest> _collectiblesRequest;
	bool _collectiblesHasMore = false;
	QString _collectiblesNextOffset;
	bool _collectiblesPaged = false;
	bool _collectiblesForced = false;
	base::flat_map<QString, Gram::NftItem> _collectibleInfo;
	base::flat_map<
		QString,
		std::vector<Fn<void(const Gram::NftItem &)>>> _collectibleInfoWaiters;
	struct CollectibleFollowUp {
		bool incoming = false;
		int left = 0;
	};
	base::flat_map<QString, CollectibleFollowUp> _collectibleFollowUps;

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
	std::string _windowSend;
	uint64 _sendRevision = 0;
	struct TransferSubmissionState {
		std::string operationId;
		std::shared_ptr<const PreparedSend> prepared;
		std::optional<TransferReceipt> receipt;
		std::optional<SendError> refusal;
		Fn<void(SendStarted)> started;
		Fn<void(TonConnectSendResult)> tonConnect;
		Fn<bool(const QString &)> tonConnectHandoff;
		FullMsgId draft;
		QByteArray normal;
		TimeId posted = 0;
		bool paired = false;
		bool normalFeeAuthorized = false;
		bool rpcStarted = false;
	};
	static void SettleTonConnect(
		TransferSubmissionState &submission,
		SendError error);
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
	// Set only across the custody write of an install, so the signing-client
	// swap that write causes keeps the scope the install was made for.
	std::shared_ptr<CommentScope> _installingScope;
	std::vector<DeferredDecrypt> _deferredDecrypts;
	rpl::lifetime _commentLifetime;

	std::unique_ptr<Ui::SeparatePanel> _panel;

	rpl::lifetime _lifetime;

};

} // namespace Wallet
