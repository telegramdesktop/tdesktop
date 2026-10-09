/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect.h"

#include "base/timer.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/delayed_activation.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect_box.h"
#include "wallet/wallet_ton_connect_request.h"
#include "window/window_session_controller.h"

#include <QtCore/QUrl>

namespace Wallet {
namespace {

using BoxPhase = TonConnectBoxPhase;

constexpr auto kWalletResolveTimeout = 20 * crl::time(1000);
constexpr auto kManifestPollInterval = 5 * crl::time(1000);
constexpr auto kManifestPollLimit = 12;
constexpr auto kNameLimit = 64;
constexpr auto kManifestNotFound = 2;
constexpr auto kManifestContent = 3;
constexpr auto kUserDeclined = 300;
constexpr auto kSessionsRefreshFloor = 60 * crl::time(1000);

struct CharRange {
	char16_t from = 0;
	char16_t till = 0;
};

constexpr auto kBidiControls = std::array{
	CharRange{ 0x200E, 0x200F },
	CharRange{ 0x202A, 0x202E },
	CharRange{ 0x2066, 0x2069 },
};

[[nodiscard]] bool IsBidiControl(QChar ch) {
	const auto code = ch.unicode();
	return ranges::any_of(kBidiControls, [&](const CharRange &range) {
		return (code >= range.from) && (code <= range.till);
	});
}

[[nodiscard]] QString Sanitize(
		const QString &name,
		const QString &fallback) {
	const auto line = TextUtilities::SingleLine(name);
	auto result = QString();
	result.reserve(line.size());
	for (const auto ch : line) {
		if (!IsBidiControl(ch)) {
			result.append(ch);
		}
	}
	if (result.size() > kNameLimit) {
		result.truncate(kNameLimit);
		if (result.at(result.size() - 1).isHighSurrogate()) {
			result.chop(1);
		}
	}
	result = result.trimmed();
	return result.isEmpty() ? fallback : result;
}

[[nodiscard]] bool SessionGone(const QString &type) {
	return (type == u"TONCONNECT_SESSION_CLOSED"_q)
		|| (type == u"TONCONNECT_SESSION_NOT_FOUND"_q);
}

[[nodiscard]] QString AccessNotice(TonConnectAccess access) {
	switch (access) {
	case TonConnectAccess::KeyChanging:
		return tr::lng_wallet_import_key_changing(tr::now);
	case TonConnectAccess::Allowed:
		return tr::lng_wallet_connect_expired(tr::now);
	case TonConnectAccess::NoCurrentKey:
	case TonConnectAccess::WalletNotReady:
	case TonConnectAccess::Busy:
		return tr::lng_wallet_connect_failed(tr::now);
	}
	Unexpected("Access in TON Connect AccessNotice.");
}

[[nodiscard]] QString DisconnectAccessNotice(TonConnectAccess access) {
	switch (access) {
	case TonConnectAccess::KeyChanging:
		return tr::lng_wallet_import_key_changing(tr::now);
	case TonConnectAccess::NoCurrentKey:
	case TonConnectAccess::Allowed:
	case TonConnectAccess::WalletNotReady:
	case TonConnectAccess::Busy:
		return tr::lng_wallet_apps_disconnect_failed(tr::now);
	}
	Unexpected("Access in TON Connect DisconnectAccessNotice.");
}

[[nodiscard]] tr::phrase<lngtag_app> TonConnectTopicPhrase(
		const QString &topic) {
	const auto list = std::array{
		std::pair{
			u"sendTransaction"_q,
			tr::lng_action_ton_connect_send_transaction },
		std::pair{ u"signData"_q, tr::lng_action_ton_connect_sign_data },
		std::pair{
			u"signMessage"_q,
			tr::lng_action_ton_connect_sign_message },
		std::pair{ u"disconnect"_q, tr::lng_action_ton_connect_disconnect },
	};
	for (const auto &[known, phrase] : list) {
		if (topic == known) {
			return phrase;
		}
	}
	return tr::lng_action_ton_connect_request;
}

[[nodiscard]] bool KeyFitsSession(
		const TonConnectSessionInfo &info,
		const TonConnectKey &key) {
	return info.clientId.isEmpty()
		|| !info.clientId.compare(key.clientId, Qt::CaseInsensitive);
}

} // namespace

class TonConnect::Connect final : public base::has_weak_ptr {
public:
	Connect(
		not_null<TonConnect*> owner,
		not_null<Window::SessionController*> controller,
		TonConnectLink link,
		QString key);
	~Connect();

	void start();
	void activate();

private:
	enum class Decision : uchar {
		None,
		Connect,
		Reject,
	};

	void resolve();
	void resolveTimeout();
	void stopResolving();
	void accessBlocked(TonConnectAccess access);
	void restorePressed();
	void restored(KeyAuthorization auth);
	void leadToSetup();
	void create();
	void created(const MTPTonConnectSession &result);
	void createFailed(const MTP::Error &error);
	void sessionChanged();
	void showManifestError(int code);
	void poll();
	void polled(const MTPwallet_TonConnectPending &result);
	void pollFailed(const MTP::Error &error);
	void connectPressed();
	void dismissed();
	void reject();
	void keyReady(TonConnectKeyResult result);
	void registerKey();
	void registered(const QByteArray &challenge, uint64 eventId);
	void registerFailed(const MTP::Error &error);
	void prepareFailed(TonConnectKeyError error);
	void submit(TonConnectReply reply);
	void submitDone(const MTPBool &result);
	void submitFailed(const QString &type);
	void failed(const QString &type = QString());
	void expired();
	void locked();
	void notice(const QString &text);
	void backToConfirm(const QString &error);
	void storeStatus(
		TonConnectSessionStatus status,
		const QString &clientId = QString());
	void closeBox();
	void finish();
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] std::shared_ptr<Main::SessionShow> showNow() const;

	const not_null<TonConnect*> _owner;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Window::SessionController> _controller;
	const TonConnectLink _link;
	const QString _key;
	MTP::Sender _api;
	std::shared_ptr<Main::SessionShow> _show;
	base::weak_qptr<Ui::GenericBox> _box;
	rpl::variable<TonConnectBoxState> _state;
	TonConnectSessionId _sessionId = 0;
	int _manifestError = 0;
	QString _domain;
	QString _address;
	VaultAuthorization _grant;
	Decision _decision = Decision::None;
	bool _closingBox = false;
	bool _polling = false;
	bool _creating = false;
	bool _retried = false;
	bool _terminal = false;
	bool _finished = false;
	int _polls = 0;
	base::Timer _resolveTimer;
	base::Timer _pollTimer;
	rpl::lifetime _resolveLifetime;
	rpl::lifetime _restoreLifetime;
	rpl::lifetime _lifetime;

};

QString TonConnectHost(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	return parsed.isValid()
		? parsed.host(QUrl::FullyEncoded).toLower()
		: QString();
}

QString TonConnectManifestName(const TonConnectManifest &manifest) {
	return Sanitize(manifest.name, TonConnectHost(manifest.url));
}

bool TonConnectSessionConnected(const TonConnectSessionInfo &info) {
	return (info.status == TonConnectSessionStatus::Active)
		|| (info.status == TonConnectSessionStatus::Closing);
}

TextWithEntities TonConnectRequestText(
		const QString &topic,
		const QString &name) {
	return name.isEmpty()
		? tr::lng_action_ton_connect_request_unknown(tr::now, tr::marked)
		: TonConnectTopicPhrase(topic)(
			tr::now,
			lt_app,
			tr::bold(name),
			tr::marked);
}

bool TonConnectRequestIdValid(const QString &id) {
	constexpr auto kLimit = 100;
	return !id.isEmpty()
		&& (id.size() <= kLimit)
		&& ranges::all_of(id, [](QChar ch) {
			return (ch.unicode() >= 0x20) && (ch.unicode() <= 0x7E);
		});
}

QString TonConnectDappName(const QString &name) {
	return Sanitize(name, QString());
}

bool TonConnectProofDomainAllowed(
		const QString &domain,
		const QString &ownershipDomain) {
	const auto normalize = [](const QString &value) {
		auto result = value.toLower();
		while (result.endsWith('.')) {
			result.chop(1);
		}
		return result;
	};
	const auto normalized = normalize(domain);
	const auto ownership = normalize(ownershipDomain);
	return !normalized.isEmpty()
		&& (normalized != u"telegram.org"_q)
		&& (ownership.isEmpty() || normalized != ownership);
}

TonConnect::TonConnect(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp())
, _requests(std::make_unique<TonConnectRequests>(session, this)) {
}

TonConnect::~TonConnect() = default;

void TonConnect::walletChanged() {
	if (_stopped) {
		return;
	}
	_closeWaiting.clear();
	// WHY: the rotated state may land before the server's pending disconnect,
	// and a key derived before the rotation must still close its sessions,
	// so the keys go only with the wallet address.
	const auto address = _session->wallet().address().value_or(QString());
	if (address.isEmpty() || address != _keysAddress) {
		_keys.clear();
		_keysAddress = address;
	}
	if (_session->wallet().presenceCurrent() == Presence::Ready) {
		requestSessions();
		return;
	}
	_api.request(base::take(_loadRequestId)).cancel();
	_sessions.clear();
	_changedWhileLoading.clear();
	_loaded = false;
	_updates.fire(0);
}

void TonConnect::vaultChanged() {
	if (_stopped) {
		return;
	}
	for (const auto id : base::take(_closeWaiting)) {
		closeSilently(id);
	}
}

void TonConnect::stop() {
	_stopped = true;
	_restoreLifetime.destroy();
	_closeWaiting.clear();
	_requests->stop();
	base::take(_connects).clear();
	_api.request(base::take(_loadRequestId)).cancel();
}

void TonConnect::apply(const MTPTonConnectSession &session) {
	store(Parse(session), false);
}

void TonConnect::applyPendingDisconnect(const QVector<MTPlong> &ids) {
	if (_stopped) {
		return;
	}
	auto reload = false;
	for (const auto &value : ids) {
		const auto id = TonConnectSessionId(value.v);
		const auto info = session(id);
		if (!info) {
			reload = true;
		} else if (info->status == TonConnectSessionStatus::Active) {
			auto copy = *info;
			copy.status = TonConnectSessionStatus::Closing;
			store(std::move(copy), false);
		} else if (info->status == TonConnectSessionStatus::Closing) {
			scheduleClose(id);
		}
	}
	if (!reload) {
		return;
	} else if (_session->wallet().presenceCurrent() == Presence::Ready) {
		requestSessions();
	} else {
		_session->wallet().ensureLoaded();
	}
}

auto TonConnect::sessions() const
-> const base::flat_map<TonConnectSessionId, TonConnectSessionInfo> & {
	return _sessions;
}

const TonConnectSessionInfo *TonConnect::session(
		TonConnectSessionId id) const {
	const auto i = _sessions.find(id);
	return (i != end(_sessions)) ? &i->second : nullptr;
}

bool TonConnect::loaded() const {
	return _loaded;
}

rpl::producer<TonConnectSessionId> TonConnect::updates() const {
	return _updates.events();
}

void TonConnect::ensureLoaded() {
	if (_stopped
		|| _loaded
		|| _loadRequestId
		|| _session->wallet().presenceCurrent() != Presence::Ready) {
		return;
	}
	requestSessions();
}

void TonConnect::refreshSessions() {
	if (_stopped
		|| _loadRequestId
		|| (_loadRequestedAt
			&& (crl::now() - _loadRequestedAt < kSessionsRefreshFloor))
		|| _session->wallet().presenceCurrent() != Presence::Ready) {
		return;
	}
	requestSessions();
}

TonConnectKey TonConnect::key(TonConnectSessionId id) const {
	const auto i = _keys.find(id);
	return (i != end(_keys)) ? i->second : TonConnectKey();
}

bool TonConnect::participates(TonConnectSessionId id) const {
	// WHY: a key derived before a rotation still speaks for its session,
	// so only a device that has to derive anew is held to the access rule.
	if (key(id)) {
		return true;
	}
	const auto access = _session->wallet().tonConnectAccess();
	return (access == TonConnectAccess::Allowed);
}

void TonConnect::acquireKey(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id,
		bool needGrant,
		Fn<void(TonConnectKeyResult)> done) {
	if (!session(id)) {
		done({ .error = TonConnectKeyError::Failed });
		return;
	}
	auto cached = key(id);
	if (cached && !needGrant) {
		done({ .key = std::move(cached) });
		return;
	}
	AcquireVaultUnlock({
		.show = std::move(show),
		.done = crl::guard(this, [=](KeyAuthorization auth) {
			unlocked(id, std::move(auth), done);
		}),
	});
}

void TonConnect::acquireSilentKey(
		TonConnectSessionId id,
		Fn<void(TonConnectKeyResult)> done) {
	using Error = TonConnectKeyError;
	if (!session(id)) {
		done({ .error = Error::Failed });
		return;
	} else if (!participates(id)) {
		done({ .error = Error::Blocked });
		return;
	} else if (auto cached = key(id)) {
		done({ .key = std::move(cached) });
		return;
	}
	auto grant = AcquireSilentVaultUnlock(_session);
	if (!grant) {
		done({ .error = Error::Locked });
		return;
	}
	unlocked(
		id,
		{ .grant = std::move(grant) },
		[=](TonConnectKeyResult result) {
			if (result.error == Error::Locked
				|| result.error == Error::Blocked) {
				result.error = Error::Failed;
			}
			done(std::move(result));
		});
}

void TonConnect::acquireClosedKey(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionInfo info,
		Fn<void(TonConnectKeyResult)> done) {
	if (info.status != TonConnectSessionStatus::Closed) {
		done({ .error = TonConnectKeyError::Failed });
		return;
	}
	AcquireVaultUnlock({
		.show = std::move(show),
		.done = crl::guard(this, [=](KeyAuthorization auth) {
			closedUnlocked(info, std::move(auth), done);
		}),
	});
}

void TonConnect::acquireKeyWith(
		TonConnectSessionId id,
		KeyAuthorization auth,
		Fn<void(TonConnectKeyResult)> done) {
	unlocked(id, std::move(auth), std::move(done));
}

bool TonConnect::disconnecting(TonConnectSessionId id) const {
	return _disconnecting.contains(id);
}

void TonConnect::disconnect(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id) {
	const auto info = session(id);
	if (_stopped
		|| !info
		|| !TonConnectSessionConnected(*info)
		|| _disconnecting.contains(id)) {
		return;
	}
	if (!participates(id)) {
		const auto access = _session->wallet().tonConnectAccess();
		if (access != TonConnectAccess::NoCurrentKey) {
			show->showToast(DisconnectAccessNotice(access));
			return;
		}
		_disconnecting.emplace(id);
		_updates.fire_copy(id);
		_restoreLifetime.destroy();
		const auto current = [=, weak = base::make_weak(this)] {
			if (!weak
				|| weak->_stopped
				|| !weak->_disconnecting.contains(id)) {
				return false;
			}
			const auto info = weak->session(id);
			return info && TonConnectSessionConnected(*info);
		};
		AcquireWalletKey(
			show,
			current,
			_restoreLifetime,
			crl::guard(this, [=](KeyAuthorization auth) {
				disconnectRestored(show, id, std::move(auth));
			}),
			tr::lng_wallet_restore_ton_connect_text());
		return;
	}
	_disconnecting.emplace(id);
	_updates.fire_copy(id);
	acquireKey(
		show,
		id,
		false,
		crl::guard(this, [=](TonConnectKeyResult result) {
			disconnectKeyReady(show, id, std::move(result));
		}));
}

void TonConnect::closeAnswered(TonConnectSessionId id) {
	if (_stopped || !session(id) || _disconnecting.contains(id)) {
		return;
	}
	_disconnecting.emplace(id);
	_updates.fire_copy(id);
	closeSession(id, QByteArray(), [=](DisconnectResult) {
		settleDisconnect(id);
	});
}

void TonConnect::connect(
		not_null<Window::SessionController*> controller,
		TonConnectLink link) {
	Expects(link.kind == TonConnectLinkKind::Connect);

	if (_stopped) {
		return;
	}
	const auto key = link.clientId.toLower();
	const auto i = _connects.find(key);
	if (i != end(_connects)) {
		i->second->activate();
		return;
	}
	const auto flow = _connects.emplace(
		key,
		std::make_unique<Connect>(this, controller, std::move(link), key)
	).first->second.get();
	flow->start();
}

TonConnectRequests &TonConnect::requests() {
	return *_requests;
}

TonConnectSessionInfo TonConnect::Parse(const MTPTonConnectSession &session) {
	const auto &data = session.data();
	auto result = TonConnectSessionInfo{
		.id = data.vid().v,
		.dappClientId = qs(data.vdapp_client_id()),
		.clientId = qs(data.vclient_id().value_or_empty()),
		.nonce = data.vnonce().v,
		.manifestError = data.vmanifest_error().value_or_empty(),
		.date = data.vdate().v,
		.status = data.is_closed()
			? TonConnectSessionStatus::Closed
			: data.is_closing()
			? TonConnectSessionStatus::Closing
			: data.is_pending()
			? TonConnectSessionStatus::Pending
			: TonConnectSessionStatus::Active,
	};
	if (const auto manifest = data.vmanifest()) {
		const auto &fields = manifest->data();
		result.manifest = TonConnectManifest{
			.url = qs(fields.vurl()),
			.name = qs(fields.vname()),
		};
		if (const auto icon = fields.vicon()) {
			icon->match([&](const MTPDwebDocument &web) {
				result.manifest->icon = WebFileLocation(
					web.vurl().v,
					web.vaccess_hash().v);
			}, [](const MTPDwebDocumentNoProxy &) {
				// A direct fetch would reveal the user's IP to the dApp.
			});
		}
	}
	return result;
}

void TonConnect::requestSessions() {
	_api.request(base::take(_loadRequestId)).cancel();
	_changedWhileLoading.clear();
	_loadRequestedAt = crl::now();
	_loadRequestId = _api.request(MTPwallet_TonConnectGetSessions(
	)).done([=](const MTPwallet_TonConnectSessions &result) {
		_loadRequestId = 0;
		_sessions.clear();
		for (const auto &session : result.data().vsessions().v) {
			auto info = Parse(session);
			if (info.status != TonConnectSessionStatus::Closed) {
				const auto id = info.id;
				_sessions.emplace_or_assign(id, std::move(info));
			}
		}
		for (auto &[info, fromCreate] : base::take(_changedWhileLoading)) {
			write(std::move(info), fromCreate);
		}
		for (auto i = begin(_keys); i != end(_keys);) {
			if (_sessions.contains(i->first)) {
				++i;
			} else {
				i = _keys.erase(i);
			}
		}
		_loaded = true;
		_updates.fire(0);
		for (const auto &[id, info] : _sessions) {
			if (info.status == TonConnectSessionStatus::Closing) {
				scheduleClose(id);
			}
		}
	}).fail([=](const MTP::Error &error) {
		_loadRequestId = 0;
		_changedWhileLoading.clear();
		LOG(("Wallet Error: wallet.tonConnectGetSessions failed: %1"
			).arg(error.type()));
	}).send();
}

void TonConnect::store(TonConnectSessionInfo info, bool fromCreate) {
	if (_loadRequestId) {
		_changedWhileLoading.emplace_back(info, fromCreate);
	}
	write(std::move(info), fromCreate);
}

void TonConnect::write(TonConnectSessionInfo info, bool fromCreate) {
	const auto id = info.id;
	if (info.status == TonConnectSessionStatus::Closed) {
		_closeWaiting.remove(id);
		const auto hadSession = _sessions.remove(id);
		const auto hadKey = _keys.remove(id);
		if (hadSession || hadKey) {
			_updates.fire_copy(id);
		}
		return;
	}
	const auto closing = (info.status == TonConnectSessionStatus::Closing);
	const auto i = _sessions.find(id);
	if (i == end(_sessions)) {
		_sessions.emplace(id, std::move(info));
		_updates.fire_copy(id);
		if (closing) {
			scheduleClose(id);
		}
		return;
	}
	const auto &stored = i->second;
	if (fromCreate
		&& !info.manifest
		&& !info.manifestError
		&& (stored.manifest || stored.manifestError)) {
		info.manifest = stored.manifest;
		info.manifestError = stored.manifestError;
	}
	if (stored == info) {
		return;
	}
	const auto wasClosing = (stored.status == TonConnectSessionStatus::Closing);
	i->second = std::move(info);
	_updates.fire_copy(id);
	if (closing && !wasClosing) {
		scheduleClose(id);
	}
}

void TonConnect::unlocked(
		TonConnectSessionId id,
		KeyAuthorization auth,
		Fn<void(TonConnectKeyResult)> done) {
	if (!auth.valid()) {
		done({ .error = TonConnectKeyError::Cancelled });
		return;
	}
	const auto info = session(id);
	if (!info) {
		done({ .error = TonConnectKeyError::Failed });
		return;
	}
	auto cached = key(id);
	if (cached) {
		done({ .key = std::move(cached), .grant = auth.grant });
		return;
	}
	const auto grant = auth.grant;
	_session->wallet().deriveTonConnectSession(
		std::move(auth),
		info->dappClientId,
		info->nonce,
		crl::guard(this, [=](TonConnectKey key) {
			derived(id, std::move(key), grant, done);
		}),
		crl::guard(this, [=](TonConnectKeyError error) {
			done({ .error = error });
		}));
}

void TonConnect::closedUnlocked(
		TonConnectSessionInfo info,
		KeyAuthorization auth,
		Fn<void(TonConnectKeyResult)> done) {
	if (!auth.valid()) {
		done({ .error = TonConnectKeyError::Cancelled });
		return;
	}
	_session->wallet().deriveTonConnectSession(
		std::move(auth),
		info.dappClientId,
		info.nonce,
		crl::guard(this, [=](TonConnectKey key) {
			if (KeyFitsSession(info, key)) {
				done({ .key = std::move(key) });
			} else {
				done({ .error = TonConnectKeyError::OtherKey });
			}
		}),
		crl::guard(this, [=](TonConnectKeyError error) {
			done({ .error = error });
		}));
}

void TonConnect::derived(
		TonConnectSessionId id,
		TonConnectKey key,
		VaultAuthorization grant,
		Fn<void(TonConnectKeyResult)> done) {
	const auto info = session(id);
	if (!info) {
		done({ .error = TonConnectKeyError::Failed });
		return;
	} else if (!KeyFitsSession(*info, key)) {
		done({ .error = TonConnectKeyError::OtherKey });
		return;
	}
	_keys[id] = key;
	done({ .key = std::move(key), .grant = std::move(grant) });
}

void TonConnect::flowDone(const QString &key, not_null<Connect*> flow) {
	crl::on_main(this, [=] {
		const auto i = _connects.find(key);
		if (i != end(_connects) && i->second.get() == flow) {
			_connects.erase(i);
		}
	});
}

void TonConnect::disconnectKeyReady(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id,
		TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	if (result.error == Error::None) {
		const auto finished = [=](DisconnectResult outcome) {
			disconnectFinished(
				show,
				id,
				((outcome == DisconnectResult::Failed)
					? tr::lng_wallet_apps_disconnect_failed(tr::now)
					: QString()));
		};
		sendDisconnect(id, std::move(result.key), finished);
		return;
	}
	auto text = QString();
	if (session(id)) {
		switch (result.error) {
		case Error::Cancelled:
			break;
		case Error::Locked:
			text = tr::lng_wallet_vault_locked(tr::now);
			break;
		case Error::Blocked:
			text = DisconnectAccessNotice(
				_session->wallet().tonConnectAccess());
			break;
		case Error::OtherKey:
			text = tr::lng_wallet_connect_request_other_key(tr::now);
			break;
		case Error::None:
		case Error::Failed:
			text = tr::lng_wallet_apps_disconnect_failed(tr::now);
			break;
		}
	}
	disconnectFinished(show, id, text);
}

void TonConnect::disconnectRestored(
		std::shared_ptr<Main::SessionShow> show,
		TonConnectSessionId id,
		KeyAuthorization auth) {
	if (_stopped) {
		return;
	} else if (!auth.grant) {
		settleDisconnect(id);
		return;
	}
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		disconnectFinished(show, id, DisconnectAccessNotice(access));
		return;
	}
	unlocked(
		id,
		KeyAuthorization{ .grant = std::move(auth.grant) },
		crl::guard(this, [=](TonConnectKeyResult result) {
			disconnectKeyReady(show, id, std::move(result));
		}));
}

void TonConnect::disconnectFinished(
		const std::shared_ptr<Main::SessionShow> &show,
		TonConnectSessionId id,
		const QString &error) {
	settleDisconnect(id);
	if (!error.isEmpty() && show->valid()) {
		show->showToast(error);
	}
}

void TonConnect::settleDisconnect(TonConnectSessionId id) {
	if (_disconnecting.remove(id) && session(id)) {
		_updates.fire_copy(id);
	}
}

void TonConnect::scheduleClose(TonConnectSessionId id) {
	if (!_stopped) {
		crl::on_main(this, [=] {
			closeSilently(id);
		});
	}
}

void TonConnect::closeSilently(TonConnectSessionId id) {
	const auto info = session(id);
	if (_stopped
		|| !info
		|| info->status != TonConnectSessionStatus::Closing) {
		_closeWaiting.remove(id);
		return;
	} else if (_disconnecting.contains(id)) {
		return;
	}
	_closeWaiting.remove(id);
	_disconnecting.emplace(id);
	_updates.fire_copy(id);
	acquireSilentKey(
		id,
		crl::guard(this, [=](TonConnectKeyResult result) {
			silentKeyReady(id, std::move(result));
		}));
}

void TonConnect::silentKeyReady(
		TonConnectSessionId id,
		TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	if (result.error == Error::None) {
		sendDisconnect(id, std::move(result.key), [=](DisconnectResult) {
			settleDisconnect(id);
		});
		return;
	}
	settleDisconnect(id);
	const auto info = session(id);
	if ((result.error == Error::Locked || result.error == Error::Blocked)
		&& info
		&& info->status == TonConnectSessionStatus::Closing) {
		_closeWaiting.emplace(id);
	}
}

void TonConnect::sendDisconnect(
		TonConnectSessionId id,
		TonConnectKey key,
		Fn<void(DisconnectResult)> done) {
	if (_stopped) {
		done(DisconnectResult::Failed);
		return;
	}
	_api.request(MTPwallet_TonConnectNextEventId(
		MTP_long(id)
	)).done([=](const MTPTonConnectNextEventId &result) {
		_session->wallet().prepareTonConnectDisconnect(
			key,
			result.data().vevent_id().v,
			crl::guard(this, [=](QByteArray body) {
				closeSession(id, std::move(body), done);
			}),
			crl::guard(this, [=] {
				done(DisconnectResult::Failed);
			}));
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.tonConnectNextEventId failed: %1"
			).arg(error.type()));
		disconnectFailed(id, error, done);
	}).send();
}

void TonConnect::closeSession(
		TonConnectSessionId id,
		QByteArray body,
		Fn<void(DisconnectResult)> done) {
	using Flag = MTPwallet_TonConnectCloseSession::Flag;
	_api.request(MTPwallet_TonConnectCloseSession(
		MTP_flags(body.isEmpty() ? Flag(0) : Flag::f_body),
		MTP_long(id),
		MTP_bytes(body)
	)).done([=](const MTPBool &result) {
		if (mtpIsTrue(result)) {
			markClosed(id);
			done(DisconnectResult::Closed);
		} else {
			LOG(("Wallet Error: "
				"wallet.tonConnectCloseSession returned false."));
			done(DisconnectResult::Failed);
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.tonConnectCloseSession failed: %1"
			).arg(error.type()));
		disconnectFailed(id, error, done);
	}).send();
}

void TonConnect::disconnectFailed(
		TonConnectSessionId id,
		const MTP::Error &error,
		const Fn<void(DisconnectResult)> &done) {
	const auto &type = error.type();
	if (SessionGone(type)
		|| (type == u"TONCONNECT_SESSION_NOT_ACTIVE"_q)) {
		markClosed(id);
		done(DisconnectResult::Closed);
	} else {
		done(MTP::IgnoreError(error)
			? DisconnectResult::Ignored
			: DisconnectResult::Failed);
	}
}

void TonConnect::markClosed(TonConnectSessionId id) {
	_disconnecting.remove(id);
	store({ .id = id, .status = TonConnectSessionStatus::Closed }, false);
}

TonConnect::Connect::Connect(
	not_null<TonConnect*> owner,
	not_null<Window::SessionController*> controller,
	TonConnectLink link,
	QString key)
: _owner(owner)
, _session(owner->_session)
, _controller(base::make_weak(controller))
, _link(std::move(link))
, _key(std::move(key))
, _api(&_session->mtp())
, _resolveTimer([=] { resolveTimeout(); })
, _pollTimer([=] { poll(); }) {
}

TonConnect::Connect::~Connect() {
	_terminal = true;
	_restoreLifetime.destroy();
	closeBox();
}

void TonConnect::Connect::start() {
	const auto controller = _controller.get();
	if (!controller) {
		finish();
		return;
	}
	_show = TonConnectBoxShow(controller);
	auto box = Box(TonConnectBox, TonConnectBoxArgs{
		.show = _show,
		.state = _state.value(),
		.connect = crl::guard(this, [=] { connectPressed(); }),
		.restore = crl::guard(this, [=] { restorePressed(); }),
		.dismissed = crl::guard(this, [=] { dismissed(); }),
	});
	_box = box.data();
	_show->showBox(std::move(box));
	if (!_box) {
		finish();
		return;
	}
	auto &wallet = _session->wallet();
	rpl::merge(
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates()
	) | rpl::on_next([=] {
		resolve();
	}, _resolveLifetime);
	_resolveTimer.callOnce(kWalletResolveTimeout);
	resolve();
	if (_resolveTimer.isActive()) {
		_polling = true;
		wallet.startPolling();
	}
}

void TonConnect::Connect::activate() {
	if (const auto box = _box.get()) {
		Ui::ActivateWindow(box->window());
	}
}

void TonConnect::Connect::resolve() {
	if (stopped()) {
		return;
	}
	auto &wallet = _session->wallet();
	const auto presence = wallet.presence();
	if (presence == Presence::Unknown
		|| (presence == Presence::Ready
			&& wallet.deviceCustodyState().mode == DeviceMode::Unknown)) {
		return;
	}
	stopResolving();
	if (presence == Presence::AddressUnreadable) {
		notice(tr::lng_wallet_state_error(tr::now));
		return;
	} else if (presence != Presence::Ready) {
		leadToSetup();
		return;
	}
	const auto access = wallet.tonConnectAccess();
	if (access == TonConnectAccess::Allowed) {
		create();
	} else {
		accessBlocked(access);
	}
}

void TonConnect::Connect::resolveTimeout() {
	if (!stopped()) {
		notice(tr::lng_wallet_state_error(tr::now));
	}
}

void TonConnect::Connect::stopResolving() {
	_resolveTimer.cancel();
	_resolveLifetime.destroy();
	if (base::take(_polling)) {
		_session->wallet().stopPolling();
	}
}

void TonConnect::Connect::accessBlocked(TonConnectAccess access) {
	if (access != TonConnectAccess::NoCurrentKey || !_box) {
		notice(AccessNotice(access));
		return;
	}
	_decision = Decision::None;
	_retried = false;
	_grant = nullptr;
	stopResolving();
	_pollTimer.cancel();
	auto state = _state.current();
	state.phase = BoxPhase::Restore;
	state.busy = false;
	state.error = QString();
	_state = std::move(state);
}

void TonConnect::Connect::restorePressed() {
	if (stopped()
		|| _state.current().phase != BoxPhase::Restore
		|| _state.current().busy) {
		return;
	}
	_restoreLifetime.destroy();
	auto state = _state.current();
	state.busy = true;
	_state = std::move(state);
	AcquireWalletKey(
		showNow(),
		[weak = base::make_weak(this)] {
			return weak && !weak->stopped() && weak->_box;
		},
		_restoreLifetime,
		crl::guard(this, [=](KeyAuthorization auth) {
			restored(std::move(auth));
		}),
		tr::lng_wallet_restore_ton_connect_text());
}

void TonConnect::Connect::restored(KeyAuthorization auth) {
	if (stopped()) {
		return;
	} else if (!auth.grant) {
		auto state = _state.current();
		if (state.phase == BoxPhase::Restore) {
			state.busy = false;
			_state = std::move(state);
		}
		return;
	}
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		notice(AccessNotice(access));
	} else if (_sessionId) {
		auto state = _state.current();
		state.busy = false;
		_state = std::move(state);
		backToConfirm(QString());
	} else {
		_state = TonConnectBoxState{ .phase = BoxPhase::Loading };
		create();
	}
}

void TonConnect::Connect::leadToSetup() {
	closeBox();
	_session->wallet().refreshState();
	ShowWallet(_session);
	finish();
}

void TonConnect::Connect::create() {
	_creating = true;
	_api.request(MTPwallet_TonConnectCreateSession(
		MTP_string(_link.clientId),
		MTP_string(_link.manifestUrl)
	)).done([=](const MTPTonConnectSession &result) {
		created(result);
	}).fail([=](const MTP::Error &error) {
		createFailed(error);
	}).send();
}

void TonConnect::Connect::created(const MTPTonConnectSession &result) {
	_creating = false;
	if (stopped()) {
		return;
	}
	auto info = Parse(result);
	const auto id = info.id;
	const auto pending = (info.status == TonConnectSessionStatus::Pending);
	const auto usable = (info.dappClientId.toLower() == _key)
		&& !info.nonce.isEmpty();
	_owner->store(std::move(info), true);
	if (!usable) {
		LOG(("Wallet Error: wallet.tonConnectCreateSession "
			"returned an unusable session."));
		notice(tr::lng_wallet_connect_failed(tr::now));
		return;
	}
	_sessionId = id;
	if (!pending) {
		closeBox();
		finish();
		return;
	} else if (_decision == Decision::Reject) {
		reject();
		return;
	}
	_owner->updates(
	) | rpl::filter([=](TonConnectSessionId changed) {
		return !changed || (changed == _sessionId);
	}) | rpl::on_next([=](TonConnectSessionId) {
		sessionChanged();
	}, _lifetime);
	sessionChanged();
	if (!stopped() && _state.current().phase == BoxPhase::Loading) {
		_pollTimer.callEach(kManifestPollInterval);
	}
}

void TonConnect::Connect::createFailed(const MTP::Error &error) {
	_creating = false;
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	LOG(("Wallet Error: wallet.tonConnectCreateSession failed: %1"
		).arg(type));
	if (!_box) {
		finish();
	} else if (type == u"WALLET_UNAVAILABLE"_q) {
		leadToSetup();
	} else if (type == u"TONCONNECT_DAPP_CLIENT_ID_INVALID"_q
		|| type == u"TONCONNECT_MANIFEST_URL_INVALID"_q) {
		closeBox();
		_show->showToast(tr::lng_wallet_send_link_invalid(tr::now));
		finish();
	} else {
		notice(ErrorWithType(tr::lng_wallet_connect_failed(tr::now), type));
	}
}

void TonConnect::Connect::sessionChanged() {
	if (stopped()) {
		return;
	}
	const auto info = _owner->session(_sessionId);
	if (!info || info->status != TonConnectSessionStatus::Pending) {
		closeBox();
		finish();
		return;
	} else if (_decision != Decision::None
		|| _state.current().phase != BoxPhase::Loading) {
		return;
	}
	// WHY: a ton_proof for telegram.org (or the ownership challenge domain)
	// is byte-for-byte the wallet ownership proof; only the domain tells the
	// two apart, so such a dApp connect is refused before anything is signed.
	if (_link.proofPayload
		&& !_session->wallet().tonConnectProofDomainAllowed(
			TonConnectHost(_link.manifestUrl))) {
		showManifestError(kManifestContent);
		return;
	} else if (info->manifestError) {
		showManifestError(info->manifestError);
		return;
	} else if (!info->manifest) {
		return;
	}
	const auto host = TonConnectHost(info->manifest->url);
	if (host.isEmpty() || host != TonConnectHost(_link.manifestUrl)) {
		showManifestError(kManifestContent);
		return;
	}
	auto &wallet = _session->wallet();
	_pollTimer.cancel();
	_domain = host;
	_address = wallet.address().value_or(QString());
	_state = TonConnectBoxState{
		.phase = BoxPhase::Confirm,
		.name = TonConnectManifestName(*info->manifest),
		.domain = _domain,
		.icon = info->manifest->icon,
		.proof = _link.proofPayload.has_value(),
	};
}

void TonConnect::Connect::showManifestError(int code) {
	_pollTimer.cancel();
	_manifestError = code;
	_state = TonConnectBoxState{
		.phase = BoxPhase::Notice,
		.notice = (code == kManifestContent)
			? tr::lng_wallet_connect_manifest_invalid(tr::now)
			: tr::lng_wallet_connect_manifest_missing(tr::now),
	};
}

void TonConnect::Connect::poll() {
	if (stopped()
		|| _decision != Decision::None
		|| _state.current().phase != BoxPhase::Loading) {
		_pollTimer.cancel();
		return;
	} else if (_polls >= kManifestPollLimit) {
		showManifestError(kManifestNotFound);
		return;
	}
	++_polls;
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_session_id),
		MTPstring(),
		MTP_long(_sessionId)
	)).done([=](const MTPwallet_TonConnectPending &result) {
		polled(result);
	}).fail([=](const MTP::Error &error) {
		pollFailed(error);
	}).send();
}

void TonConnect::Connect::polled(const MTPwallet_TonConnectPending &result) {
	if (!stopped()) {
		_owner->apply(result.data().vsession());
	}
}

void TonConnect::Connect::pollFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	LOG(("Wallet Error: wallet.tonConnectGetPending failed: %1"
		).arg(type));
	if (SessionGone(type)) {
		storeStatus(TonConnectSessionStatus::Closed);
	}
}

void TonConnect::Connect::connectPressed() {
	if (stopped()
		|| _decision != Decision::None
		|| _state.current().phase != BoxPhase::Confirm) {
		return;
	}
	const auto address = _session->wallet().address();
	if (address.value_or(QString()) != _address) {
		notice(tr::lng_wallet_connect_expired(tr::now));
		return;
	}
	_decision = Decision::Connect;
	auto state = _state.current();
	state.phase = BoxPhase::Connecting;
	state.error = QString();
	_state = std::move(state);
	_owner->acquireKey(
		showNow(),
		_sessionId,
		_link.proofPayload.has_value(),
		crl::guard(this, [=](TonConnectKeyResult result) {
			keyReady(std::move(result));
		}));
}

void TonConnect::Connect::dismissed() {
	if (_closingBox || _finished) {
		return;
	}
	_box.reset();
	_pollTimer.cancel();
	if (_terminal
		|| (!_sessionId && !_creating)
		|| _state.current().phase == BoxPhase::Restore) {
		finish();
	} else if (_decision == Decision::Connect) {
		return;
	} else if (_creating) {
		_decision = Decision::Reject;
	} else {
		_decision = Decision::Reject;
		crl::on_main(this, [=] {
			reject();
		});
	}
}

void TonConnect::Connect::reject() {
	if (stopped()) {
		return;
	}
	_owner->acquireKey(
		showNow(),
		_sessionId,
		false,
		crl::guard(this, [=](TonConnectKeyResult result) {
			keyReady(std::move(result));
		}));
}

void TonConnect::Connect::keyReady(TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	if (stopped()) {
		return;
	} else if (result.error == Error::None) {
		_grant = std::move(result.grant);
		registerKey();
		return;
	} else if (_decision == Decision::Reject) {
		if (result.error != Error::Cancelled) {
			LOG(("Wallet Error: TON Connect rejection was not sent."));
		}
		finish();
		return;
	}
	switch (result.error) {
	case Error::Cancelled:
		backToConfirm(QString());
		return;
	case Error::Locked:
		locked();
		return;
	case Error::Blocked:
		accessBlocked(_session->wallet().tonConnectAccess());
		return;
	case Error::OtherKey:
		notice(tr::lng_wallet_connect_expired(tr::now));
		return;
	case Error::None:
	case Error::Failed:
		backToConfirm(tr::lng_wallet_connect_failed(tr::now));
		return;
	}
	Unexpected("Error in TonConnect::Connect::keyReady.");
}

void TonConnect::Connect::registerKey() {
	const auto key = _owner->key(_sessionId);
	if (!key) {
		failed();
		return;
	}
	_api.request(MTPwallet_TonConnectRegisterKey(
		MTP_long(_sessionId),
		MTP_string(key.clientId)
	)).done([=](const MTPwallet_TonConnectChallenge &result) {
		const auto &data = result.data();
		registered(data.vchallenge().v, data.vevent_id().v);
	}).fail([=](const MTP::Error &error) {
		registerFailed(error);
	}).send();
}

void TonConnect::Connect::registered(
		const QByteArray &challenge,
		uint64 eventId) {
	if (stopped()) {
		return;
	}
	auto &wallet = _session->wallet();
	const auto ready = crl::guard(this, [=](TonConnectReply reply) {
		submit(std::move(reply));
	});
	if (_decision == Decision::Reject) {
		wallet.prepareTonConnectError(
			_owner->key(_sessionId),
			challenge,
			eventId,
			_manifestError ? _manifestError : kUserDeclined,
			ready,
			crl::guard(this, [=] { failed(); }));
		return;
	}
	wallet.prepareTonConnectEvent(
		{ .grant = _grant },
		_owner->key(_sessionId),
		{
			.challenge = challenge,
			.eventId = eventId,
			.address = _address,
			.proofDomain = _domain,
			.proofPayload = _link.proofPayload,
		},
		ready,
		crl::guard(this, [=](TonConnectKeyError error) {
			prepareFailed(error);
		}));
}

void TonConnect::Connect::registerFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	LOG(("Wallet Error: wallet.tonConnectRegisterKey failed: %1"
		).arg(type));
	if (type == u"TONCONNECT_CLIENT_ID_OCCUPIED"_q) {
		notice(tr::lng_wallet_connect_expired(tr::now));
	} else if (SessionGone(type)) {
		expired();
	} else {
		failed(type);
	}
}

void TonConnect::Connect::prepareFailed(TonConnectKeyError error) {
	if (stopped()) {
		return;
	} else if (error == TonConnectKeyError::Blocked) {
		accessBlocked(_session->wallet().tonConnectAccess());
	} else if (error == TonConnectKeyError::Locked) {
		locked();
	} else {
		failed();
	}
}

void TonConnect::Connect::submit(TonConnectReply reply) {
	if (stopped()) {
		return;
	}
	using Flag = MTPwallet_TonConnectSubmitConnectResult::Flag;
	const auto rejecting = (_decision == Decision::Reject);
	_api.request(MTPwallet_TonConnectSubmitConnectResult(
		MTP_flags((rejecting ? Flag::f_error : Flag(0))
			| (_link.traceId.isEmpty() ? Flag(0) : Flag::f_trace_id)),
		MTP_long(_sessionId),
		MTP_bytes(reply.challengeAnswer),
		MTP_bytes(reply.body),
		MTP_string(_link.traceId)
	)).done([=](const MTPBool &result) {
		submitDone(result);
	}).fail([=](const MTP::Error &error) {
		submitFailed(error.type());
	}).send();
}

void TonConnect::Connect::submitDone(const MTPBool &result) {
	if (stopped()) {
		return;
	} else if (!mtpIsTrue(result)) {
		submitFailed(QString());
		return;
	} else if (_decision == Decision::Reject) {
		storeStatus(TonConnectSessionStatus::Closed);
		finish();
		return;
	}
	storeStatus(
		TonConnectSessionStatus::Active,
		_owner->key(_sessionId).clientId);
	_grant = nullptr;
	closeBox();
	finish();
}

void TonConnect::Connect::submitFailed(const QString &type) {
	if (stopped()) {
		return;
	}
	LOG(("Wallet Error: wallet.tonConnectSubmitConnectResult failed: %1"
		).arg(type.isEmpty() ? u"FALSE"_q : type));
	const auto retryable = (type == u"TONCONNECT_CHALLENGE_INVALID"_q)
		|| (type == u"TONCONNECT_SESSION_NOT_ACTIVE"_q);
	if (retryable && !_retried) {
		_retried = true;
		registerKey();
	} else if ((_decision == Decision::Connect)
		&& (type == u"TONCONNECT_SESSION_NOT_FOUND"_q)) {
		storeStatus(TonConnectSessionStatus::Closed);
		closeBox();
		finish();
	} else if (SessionGone(type)) {
		expired();
	} else {
		failed(type);
	}
}

void TonConnect::Connect::failed(const QString &type) {
	if (_decision == Decision::Reject) {
		finish();
	} else {
		backToConfirm(ErrorWithType(
			tr::lng_wallet_connect_failed(tr::now),
			type));
	}
}

void TonConnect::Connect::expired() {
	_terminal = true;
	storeStatus(TonConnectSessionStatus::Closed);
	notice(tr::lng_wallet_connect_expired(tr::now));
}

void TonConnect::Connect::locked() {
	if (_box) {
		_show->showToast(tr::lng_wallet_vault_locked(tr::now));
	}
	backToConfirm(QString());
}

void TonConnect::Connect::notice(const QString &text) {
	_terminal = true;
	_decision = Decision::None;
	_grant = nullptr;
	stopResolving();
	_pollTimer.cancel();
	if (!_box) {
		finish();
		return;
	}
	auto state = TonConnectBoxState{
		.phase = BoxPhase::Notice,
		.name = _state.current().name,
		.domain = _state.current().domain,
		.icon = _state.current().icon,
		.notice = text,
	};
	_state = std::move(state);
}

void TonConnect::Connect::backToConfirm(const QString &error) {
	_decision = Decision::None;
	_retried = false;
	_grant = nullptr;
	if (!_box) {
		LOG(("Wallet Error: TON Connect connection stopped "
			"after its box was closed."));
		finish();
		return;
	}
	auto state = _state.current();
	state.phase = BoxPhase::Confirm;
	state.error = error;
	_state = std::move(state);
}

void TonConnect::Connect::storeStatus(
		TonConnectSessionStatus status,
		const QString &clientId) {
	const auto now = _owner->session(_sessionId);
	if (!now && status != TonConnectSessionStatus::Closed) {
		return;
	}
	auto info = now ? *now : TonConnectSessionInfo{ .id = _sessionId };
	info.status = status;
	if (!clientId.isEmpty()) {
		info.clientId = clientId;
	}
	_owner->store(std::move(info), false);
}

void TonConnect::Connect::closeBox() {
	_closingBox = true;
	if (const auto box = _box.get()) {
		_box.reset();
		if (box->hasDelegate()) {
			box->closeBox();
		}
	}
}

void TonConnect::Connect::finish() {
	if (std::exchange(_finished, true)) {
		return;
	}
	_restoreLifetime.destroy();
	_grant = nullptr;
	stopResolving();
	_pollTimer.cancel();
	_lifetime.destroy();
	_owner->flowDone(_key, this);
}

bool TonConnect::Connect::stopped() const {
	return _terminal || _finished;
}

std::shared_ptr<Main::SessionShow> TonConnect::Connect::showNow() const {
	if (_show->valid()) {
		return _show;
	} else if (const auto controller = _controller.get()) {
		return TonConnectBoxShowNoActivate(controller);
	}
	return _show;
}

} // namespace Wallet
