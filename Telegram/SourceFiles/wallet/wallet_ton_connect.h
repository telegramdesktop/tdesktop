/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/weak_ptr.h"
#include "mtproto/sender.h"
#include "ui/image/image_location.h"
#include "wallet/wallet_ton_connect_link.h"
#include "wallet/wallet_unlock.h"

struct TextWithEntities;

namespace wallet_engine {
struct SendRequest;
struct TonConnectDerivedSession;
struct TonConnectSignDataRequest;
} // namespace wallet_engine

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Wallet {

class TonConnectRequests;

using TonConnectSessionId = uint64;

enum class TonConnectAccess : uchar {
	Allowed,
	WalletNotReady,
	KeyChanging,
	NoCurrentKey,
	Busy,
};

enum class TonConnectKeyError : uchar {
	None,
	Blocked,
	Cancelled,
	Locked,
	OtherKey,
	Failed,
};

struct TonConnectKey {
	std::shared_ptr<wallet_engine::TonConnectDerivedSession> session;
	QString clientId;
	QByteArray signingKey;

	explicit operator bool() const {
		return (session != nullptr);
	}
};

struct TonConnectEventRequest {
	QByteArray challenge;
	uint64 eventId = 0;
	QString address;
	QString proofDomain;
	std::optional<QString> proofPayload;
};

struct TonConnectReply {
	QByteArray challengeAnswer;
	QByteArray body;
};

struct TonConnectKeyResult {
	TonConnectKey key;
	VaultAuthorization grant;
	TonConnectKeyError error = TonConnectKeyError::None;
};

struct TonConnectManifest {
	QString url;
	QString name;
	WebFileLocation icon;

	friend bool operator==(
		const TonConnectManifest &,
		const TonConnectManifest &) = default;
};

enum class TonConnectSessionStatus : uchar {
	Pending,
	Active,
	Closing,
	Closed,
};

struct TonConnectSessionInfo {
	TonConnectSessionId id = 0;
	QString dappClientId;
	QString clientId;
	QByteArray nonce;
	std::optional<TonConnectManifest> manifest;
	int manifestError = 0;
	TimeId date = 0;
	TonConnectSessionStatus status = TonConnectSessionStatus::Pending;

	friend bool operator==(
		const TonConnectSessionInfo &,
		const TonConnectSessionInfo &) = default;
};

struct TonConnectMessage {
	QString destination;
	int64 amountNano = 0;
	QString comment;
	QString payload;
	bool deploys = false;
};

struct TonConnectTransfer {
	std::shared_ptr<const wallet_engine::SendRequest> request;
	std::vector<TonConnectMessage> messages;
	int64 totalNano = 0;
	std::optional<TimeId> validUntil;
};

enum class TonConnectSignDataType : uchar {
	Text,
	Binary,
	Cell,
};

struct TonConnectSignDataField {
	int depth = 0;
	QString name;
	QString value;
};

struct TonConnectSignData {
	std::shared_ptr<const wallet_engine::TonConnectSignDataRequest> request;
	TonConnectSignDataType type = TonConnectSignDataType::Text;
	QString data;
	QString schema;
	std::vector<TonConnectSignDataField> fields;
};

enum class TonConnectRequestKind : uchar {
	SendTransaction,
	Invalid,
	Disconnect,
	Unsupported,
	SignData,
};

enum class TonConnectError : uchar {
	Unknown,
	BadRequest,
	UserDeclined,
	UnknownApp,
	MethodNotSupported,
};

struct TonConnectAppRequest {
	TonConnectRequestKind kind = TonConnectRequestKind::Unsupported;
	QString id;
	QString method;
	std::shared_ptr<const TonConnectTransfer> transfer;
	std::shared_ptr<const TonConnectSignData> signData;
	TonConnectError rejection = TonConnectError::BadRequest;
};

struct TonConnectResponse {
	QString signedBoc;
	TonConnectError error = TonConnectError::Unknown;
	bool disconnected = false;
};

[[nodiscard]] QString TonConnectHost(const QString &url);
[[nodiscard]] QString TonConnectManifestName(
	const TonConnectManifest &manifest);
[[nodiscard]] bool TonConnectSessionConnected(
	const TonConnectSessionInfo &info);
[[nodiscard]] TextWithEntities TonConnectRequestText(
	const QString &topic,
	const QString &name);
[[nodiscard]] bool TonConnectRequestIdValid(const QString &id);
[[nodiscard]] QString TonConnectDappName(const QString &name);
[[nodiscard]] bool TonConnectProofDomainAllowed(
	const QString &domain,
	const QString &ownershipDomain);

class TonConnect final : public base::has_weak_ptr {
public:
	explicit TonConnect(not_null<Main::Session*> session);
	~TonConnect();

	void walletChanged();
	void vaultChanged();
	void stop();

	void apply(const MTPTonConnectSession &session);
	void applyPendingDisconnect(const QVector<MTPlong> &ids);
	[[nodiscard]] static TonConnectSessionInfo Parse(
		const MTPTonConnectSession &session);

	[[nodiscard]] auto sessions() const
		-> const base::flat_map<TonConnectSessionId, TonConnectSessionInfo> &;
	[[nodiscard]] const TonConnectSessionInfo *session(
		TonConnectSessionId id) const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] rpl::producer<TonConnectSessionId> updates() const;
	void ensureLoaded();
	void refreshSessions();

	[[nodiscard]] TonConnectKey key(TonConnectSessionId id) const;
	[[nodiscard]] bool participates(TonConnectSessionId id) const;
	void acquireKey(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id,
		bool needGrant,
		Fn<void(TonConnectKeyResult)> done);
	void acquireSilentKey(
		TonConnectSessionId id,
		Fn<void(TonConnectKeyResult)> done);
	void acquireClosedKey(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionInfo info,
		Fn<void(TonConnectKeyResult)> done);
	void acquireKeyWith(
		TonConnectSessionId id,
		KeyAuthorization auth,
		Fn<void(TonConnectKeyResult)> done);
	[[nodiscard]] bool disconnecting(TonConnectSessionId id) const;
	void disconnect(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id);
	void closeAnswered(TonConnectSessionId id);

	void connect(
		not_null<Window::SessionController*> controller,
		TonConnectLink link);

	[[nodiscard]] TonConnectRequests &requests();

private:
	class Connect;
	enum class DisconnectResult : uchar {
		Closed,
		Failed,
		Ignored,
	};

	void requestSessions();
	void store(TonConnectSessionInfo info, bool fromCreate);
	void write(TonConnectSessionInfo info, bool fromCreate);
	void unlocked(
		TonConnectSessionId id,
		KeyAuthorization auth,
		Fn<void(TonConnectKeyResult)> done);
	void closedUnlocked(
		TonConnectSessionInfo info,
		KeyAuthorization auth,
		Fn<void(TonConnectKeyResult)> done);
	void derived(
		TonConnectSessionId id,
		TonConnectKey key,
		VaultAuthorization grant,
		Fn<void(TonConnectKeyResult)> done);
	void flowDone(const QString &key, not_null<Connect*> flow);
	void disconnectKeyReady(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id,
		TonConnectKeyResult result);
	void disconnectFinished(
		const std::shared_ptr<Main::SessionShow> &show,
		TonConnectSessionId id,
		const QString &error);
	void disconnectRestored(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id,
		KeyAuthorization auth);
	void settleDisconnect(TonConnectSessionId id);
	void scheduleClose(TonConnectSessionId id);
	void closeSilently(TonConnectSessionId id);
	void silentKeyReady(TonConnectSessionId id, TonConnectKeyResult result);
	void sendDisconnect(
		TonConnectSessionId id,
		TonConnectKey key,
		Fn<void(DisconnectResult)> done);
	void closeSession(
		TonConnectSessionId id,
		QByteArray body,
		Fn<void(DisconnectResult)> done);
	void disconnectFailed(
		TonConnectSessionId id,
		const MTP::Error &error,
		const Fn<void(DisconnectResult)> &done);
	void markClosed(TonConnectSessionId id);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::flat_map<TonConnectSessionId, TonConnectSessionInfo> _sessions;
	base::flat_map<TonConnectSessionId, TonConnectKey> _keys;
	QString _keysAddress;
	base::flat_map<QString, std::unique_ptr<Connect>> _connects;
	std::vector<std::pair<TonConnectSessionInfo, bool>> _changedWhileLoading;
	base::flat_set<TonConnectSessionId> _disconnecting;
	base::flat_set<TonConnectSessionId> _closeWaiting;
	rpl::lifetime _restoreLifetime;
	rpl::event_stream<TonConnectSessionId> _updates;
	crl::time _loadRequestedAt = 0;
	mtpRequestId _loadRequestId = 0;
	bool _loaded = false;
	bool _stopped = false;
	std::unique_ptr<TonConnectRequests> _requests;

};

} // namespace Wallet
