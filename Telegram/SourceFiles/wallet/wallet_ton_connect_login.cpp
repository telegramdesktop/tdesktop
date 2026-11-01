/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_login.h"

#include "base/timer_rpl.h"
#include "boxes/url_auth_box_content.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect.h"

#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_url_auth_box_content.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kWalletResolveTimeout = 20 * crl::time(1000);
constexpr auto kWalletPreloadTimeout = 3 * crl::time(1000);
constexpr auto kManifestPollInterval = 5 * crl::time(1000);
constexpr auto kManifestPollLimit = 12;
constexpr auto kManifestContent = 3;

[[nodiscard]] bool WalletStateKnown(not_null<Main::Session*> session) {
	auto &wallet = session->wallet();
	const auto presence = wallet.presence();
	return (presence != Presence::Unknown)
		&& (presence != Presence::Ready
			|| wallet.deviceCustodyState().mode != DeviceMode::Unknown);
}

} // namespace

std::optional<TonConnectLoginRequest> ParseTonConnectLogin(
		const MTPDurlAuthResultRequest &data) {
	if (!data.is_request_wallet()) {
		return std::nullopt;
	}
	const auto sessionId = data.vtc_session_id();
	const auto clientId = qs(data.vtc_client_id().value_or_empty());
	const auto domain = qs(data.vdomain());
	if (!sessionId || clientId.isEmpty() || domain.isEmpty()) {
		LOG(("Wallet Error: a login asked "
			"for the wallet without a TON Connect session."));
		return std::nullopt;
	}
	return TonConnectLoginRequest{
		.sessionId = sessionId->v,
		.clientId = clientId,
		.domain = domain,
		.botId = data.vbot().match([](const auto &user) {
			return user.vid().v;
		}),
	};
}

void PreloadTonConnectLoginWallet(
		not_null<Main::Session*> session,
		Fn<void()> done,
		Fn<void()> lost) {
	if (WalletStateKnown(session)) {
		done();
		return;
	}
	auto &wallet = session->wallet();
	wallet.startPolling();
	const auto lifetime = std::make_shared<rpl::lifetime>();
	session->account().sessionChanges(
	) | rpl::filter(
		!rpl::mappers::_1
	) | rpl::on_next([=] {
		const auto callback = lost;
		lifetime->destroy();
		if (callback) {
			callback();
		}
	}, *lifetime);
	rpl::merge(
		base::timer_once(kWalletPreloadTimeout),
		rpl::merge(
			wallet.presenceValue() | rpl::to_empty,
			wallet.deviceCustodyStateValue() | rpl::to_empty
		) | rpl::filter([=] {
			return WalletStateKnown(session);
		})
	) | rpl::on_next([=] {
		const auto callback = done;
		session->wallet().stopPolling();
		lifetime->destroy();
		callback();
	}, *lifetime);
}

TonConnectLoginSection::TonConnectLoginSection(
	not_null<Ui::VerticalLayout*> container,
	TonConnectLoginArgs &&args)
: _container(container)
, _session(args.session)
, _show(std::move(args.show))
, _request(std::move(args.request))
, _manifestUrl(
	u"https://"_q + _request.domain + u"/tonconnect-manifest.json"_q)
, _busyChanged(std::move(args.busyChanged))
, _api(&args.session->mtp())
, _botName(std::move(args.botName))
, _resolveTimer([=] {
	_timedOut = true;
	refresh();
})
, _manifestTimer([=] { pollManifest(); }) {
	setup();
}

TonConnectLoginSection::~TonConnectLoginSection() {
	invalidate_weak_ptrs(this);
	_restoreLifetime.destroy();
	stopResolving();
}

bool TonConnectLoginSection::wanted() const {
	return _toggled.current();
}

bool TonConnectLoginSection::busy() const {
	return _busy;
}

void TonConnectLoginSection::confirm(
		Fn<void(MTPInputTonConnectOauthSession)> ready) {
	if (_busy) {
		return;
	}
	_ready = std::move(ready);
	_error->hide(anim::type::normal);
	setBusy(true);
	if (_state.current() == State::Loading) {
		_queued = true;
		return;
	}
	start();
}

rpl::producer<TextWithEntities> TonConnectLoginSection::StateReason(
		State state) {
	switch (state) {
	case State::NoKey:
		return tr::lng_url_auth_wallet_no_key(
			lt_link,
			tr::lng_url_auth_wallet_no_key_link(tr::link),
			tr::marked);
	case State::KeyChanging:
		return tr::lng_wallet_import_key_changing(tr::marked);
	case State::Unavailable:
		return tr::lng_wallet_connect_failed(tr::marked);
	case State::LoadFailed:
		return tr::lng_wallet_state_error(tr::marked);
	case State::Reserved:
		return tr::lng_url_auth_wallet_reserved(tr::marked);
	case State::Loading:
	case State::Allowed:
		break;
	}
	return rpl::single(tr::marked());
}

void TonConnectLoginSection::setup() {
	_rowSlot = _container->add(object_ptr<Ui::VerticalLayout>(_container));
	_error = _container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			_container,
			object_ptr<Ui::FlatLabel>(
				_container,
				st::walletConnectErrorLabel),
			style::margins(0, st::defaultVerticalListSkip, 0, 0)),
		st::boxRowPadding,
		style::al_top);
	_error->hide(anim::type::instant);
	_toggle = _container->add(object_ptr<Ui::SettingsButton>(
		_container,
		tr::lng_url_auth_wallet_connect()));
	_toggle->toggleOn(_toggled.value(), true);
	_toggle->setClickedCallback([=] {
		if (!_toggle->isDisabled()) {
			_wanted = !_toggled.current();
			_toggled = _wanted;
		}
	});
	Ui::AddSkip(_container);

	const auto join = [](TextWithEntities text, TextWithEntities suffix) {
		if (!suffix.empty()) {
			text.append(u"\n\n"_q).append(std::move(suffix));
		}
		return text;
	};
	const auto about = Ui::AddDividerText(
		_container,
		rpl::combine(
			tr::lng_url_auth_wallet_about(
				lt_bot,
				_botName.value() | rpl::map(tr::marked),
				tr::marked),
			_state.value() | rpl::map(StateReason) | rpl::flatten_latest()
		) | rpl::map(join));
	about->setClickHandlerFilter([=](const auto &...) {
		restorePressed();
		return false;
	});
	Ui::AddSkip(_container);

	auto &wallet = _session->wallet();
	rpl::merge(
		wallet.presenceValue() | rpl::to_empty,
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates(),
		wallet.deviceCustodyStateValue() | rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, _container->lifetime());
}

TonConnectLoginSection::State TonConnectLoginSection::computeState() {
	auto &wallet = _session->wallet();
	const auto presence = wallet.presence();
	if (!WalletStateKnown(_session)) {
		return _timedOut ? State::LoadFailed : State::Loading;
	} else if (presence != Presence::Ready) {
		return State::LoadFailed;
	} else if (!wallet.tonConnectProofDomainAllowed(_request.domain)) {
		return State::Reserved;
	}
	switch (wallet.tonConnectAccess()) {
	case TonConnectAccess::Allowed: return State::Allowed;
	case TonConnectAccess::NoCurrentKey: return State::NoKey;
	case TonConnectAccess::KeyChanging: return State::KeyChanging;
	case TonConnectAccess::WalletNotReady:
	case TonConnectAccess::Busy:
		break;
	}
	return State::Unavailable;
}

void TonConnectLoginSection::refresh() {
	const auto state = computeState();
	if (state == State::Loading) {
		if (!_polling) {
			_polling = true;
			_session->wallet().startPolling();
		}
		if (!_resolveTimer.isActive()) {
			_resolveTimer.callOnce(kWalletResolveTimeout);
		}
	} else {
		stopResolving();
	}
	applyState(state);
}

void TonConnectLoginSection::applyState(State state) {
	_state = state;
	rebuildRow();
	const auto usable = (state == State::Allowed)
		|| (state == State::Loading);
	_toggled = usable && _wanted;
	_toggle->setDisabled(_busy || !usable);
	if (_queued && state != State::Loading) {
		_queued = false;
		if (state == State::Allowed) {
			start();
		} else {
			fail(tr::lng_wallet_connect_failed(tr::now));
		}
	}
}

void TonConnectLoginSection::rebuildRow() {
	const auto friendly = _session->wallet().addressFriendly(false);
	if (friendly == _rowAddress) {
		return;
	}
	_rowAddress = friendly;
	const auto slot = _rowSlot.data();
	slot->clear();
	if (friendly.isEmpty()) {
		return;
	}
	const auto half = friendly.size() / 2;
	const auto code = [](const QString &text) {
		return Ui::Text::Wrapped(tr::marked(text), EntityType::Code);
	};
	const auto labels = UrlAuthBox::AddAuthInfoRow(
		slot,
		code(friendly.left(half)),
		code(friendly.mid(half)),
		tr::lng_url_auth_wallet_label(tr::now),
		st::menuIconTon,
		&st::urlAuthBoxRowAddressLabel);
	Ui::AddSkip(slot);
	Ui::AddSkip(slot);
	const auto copy = CopyTextCallback(
		_show,
		friendly,
		tr::lng_gift_unique_address_copied(tr::now));
	const auto filter = [=](const auto &...) {
		copy();
		return false;
	};
	labels.top->setClickHandlerFilter(filter);
	labels.bottom->setClickHandlerFilter(filter);
}

void TonConnectLoginSection::stopResolving() {
	_resolveTimer.cancel();
	if (base::take(_polling)) {
		_session->wallet().stopPolling();
	}
}

void TonConnectLoginSection::restorePressed() {
	if (_state.current() != State::NoKey || _restoring || _busy) {
		return;
	}
	_restoreLifetime.destroy();
	_restoring = true;
	AcquireWalletKey(
		_show,
		[weak = base::make_weak(this)] {
			return weak && (weak->_state.current() == State::NoKey);
		},
		_restoreLifetime,
		crl::guard(this, [=](KeyAuthorization auth) {
			restored(std::move(auth));
		}),
		tr::lng_wallet_restore_ton_connect_text());
}

void TonConnectLoginSection::restored(KeyAuthorization auth) {
	_restoring = false;
	if (auth.grant) {
		_wanted = true;
	}
	refresh();
}

void TonConnectLoginSection::start() {
	if (_state.current() != State::Allowed) {
		fail(tr::lng_wallet_connect_failed(tr::now));
		return;
	}
	auto &wallet = _session->wallet();
	_address = wallet.address().value_or(QString());
	if (_address.isEmpty() || wallet.addressFriendly(false) != _rowAddress) {
		refresh();
		fail(tr::lng_wallet_connect_failed(tr::now));
		return;
	}
	AcquireVaultUnlock({
		.show = _show,
		.done = crl::guard(this, [=](KeyAuthorization auth) {
			unlocked(std::move(auth));
		}),
	});
}

void TonConnectLoginSection::unlocked(KeyAuthorization auth) {
	if (!auth.valid()) {
		fail(QString());
		return;
	}
	_grant = auth.grant;
	create();
}

void TonConnectLoginSection::create() {
	_api.request(MTPwallet_TonConnectCreateSession(
		MTP_string(_request.clientId),
		MTP_string(_manifestUrl)
	)).done([=](const MTPTonConnectSession &result) {
		created(result);
	}).fail([=](const MTP::Error &error) {
		createFailed(error);
	}).send();
}

void TonConnectLoginSection::created(const MTPTonConnectSession &result) {
	auto &store = _session->wallet().tonConnect();
	store.applyCreated(result);
	const auto info = TonConnect::Parse(result);
	const auto usable = (info.id == _request.sessionId)
		&& !info.dappClientId.compare(_request.clientId, Qt::CaseInsensitive)
		&& !info.nonce.isEmpty()
		&& (info.status == TonConnectSessionStatus::Pending);
	if (!usable) {
		LOG(("Wallet Error: wallet.tonConnectCreateSession "
			"returned another session for a login."));
		fail(tr::lng_wallet_connect_failed(tr::now));
		return;
	}
	_manifestPolls = 0;
	store.updates(
	) | rpl::filter([=](TonConnectSessionId id) {
		return !id || (id == _request.sessionId);
	}) | rpl::on_next([=](TonConnectSessionId) {
		manifestChanged();
	}, _manifestLifetime);
	manifestChanged();
}

void TonConnectLoginSection::createFailed(const MTP::Error &error) {
	const auto &type = error.type();
	LOG(("Wallet Error: login wallet.tonConnectCreateSession failed: %1"
		).arg(type));
	if (type == u"WALLET_UNAVAILABLE"_q) {
		refresh();
	}
	fail(MTP::IgnoreError(error)
		? QString()
		: ErrorWithType(tr::lng_wallet_connect_failed(tr::now), type));
}

void TonConnectLoginSection::manifestChanged() {
	auto &store = _session->wallet().tonConnect();
	const auto info = store.session(_request.sessionId);
	if (!info || info->status != TonConnectSessionStatus::Pending) {
		fail(tr::lng_wallet_connect_expired(tr::now));
		return;
	} else if (info->manifestError) {
		fail((info->manifestError == kManifestContent)
			? tr::lng_wallet_connect_manifest_invalid(tr::now)
			: tr::lng_wallet_connect_manifest_missing(tr::now));
		return;
	} else if (!info->manifest) {
		if (!_manifestTimer.isActive()) {
			_manifestTimer.callEach(kManifestPollInterval);
		}
		return;
	}
	const auto host = TonConnectHost(info->manifest->url);
	if (host.isEmpty() || host != TonConnectHost(_manifestUrl)) {
		LOG(("Wallet Error: a login session's manifest "
			"belongs to another domain."));
		fail(tr::lng_wallet_connect_manifest_invalid(tr::now));
		return;
	}
	stopManifestWait();
	store.acquireKeyWith(
		_request.sessionId,
		KeyAuthorization{ .grant = _grant },
		crl::guard(this, [=](TonConnectKeyResult result) {
			keyReady(std::move(result));
		}));
}

void TonConnectLoginSection::pollManifest() {
	if (_manifestPolls >= kManifestPollLimit) {
		fail(tr::lng_wallet_connect_manifest_missing(tr::now));
		return;
	}
	++_manifestPolls;
	if (_manifestRequestId) {
		return;
	}
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_manifestRequestId = _api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_session_id),
		MTPstring(),
		MTP_long(_request.sessionId)
	)).done([=](const MTPwallet_TonConnectPending &result) {
		_manifestRequestId = 0;
		_session->wallet().tonConnect().apply(result.data().vsession());
	}).fail([=](const MTP::Error &error) {
		_manifestRequestId = 0;
		manifestPollFailed(error);
	}).send();
}

void TonConnectLoginSection::manifestPollFailed(const MTP::Error &error) {
	const auto &type = error.type();
	LOG(("Wallet Error: login wallet.tonConnectGetPending failed: %1"
		).arg(type));
	if (MTP::IgnoreError(error)) {
		fail(QString());
	} else if (type == u"TONCONNECT_SESSION_CLOSED"_q
		|| type == u"TONCONNECT_SESSION_NOT_FOUND"_q) {
		fail(tr::lng_wallet_connect_expired(tr::now));
	}
}

void TonConnectLoginSection::stopManifestWait() {
	_manifestLifetime.destroy();
	_manifestTimer.cancel();
	_api.request(base::take(_manifestRequestId)).cancel();
}

void TonConnectLoginSection::keyReady(TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	switch (result.error) {
	case Error::None:
		registerKey(std::move(result.key));
		return;
	case Error::Cancelled:
		fail(QString());
		return;
	case Error::Locked:
		_show->showToast(tr::lng_wallet_vault_locked(tr::now));
		fail(QString());
		return;
	case Error::Blocked:
		refresh();
		break;
	case Error::OtherKey:
	case Error::Failed:
		break;
	}
	fail(tr::lng_wallet_connect_failed(tr::now));
}

void TonConnectLoginSection::registerKey(TonConnectKey key) {
	_api.request(MTPwallet_TonConnectRegisterKey(
		MTP_long(_request.sessionId),
		MTP_string(key.clientId)
	)).done([=](const MTPwallet_TonConnectChallenge &result) {
		const auto &data = result.data();
		registered(key, data.vchallenge().v, data.vevent_id().v);
	}).fail([=](const MTP::Error &error) {
		registerFailed(error);
	}).send();
}

void TonConnectLoginSection::registered(
		TonConnectKey key,
		QByteArray challenge,
		uint64 eventId) {
	const auto payload = u"tc-login:%1:%2:%3"_q.arg(
		QString::number(_request.botId),
		_request.clientId,
		QString::number(int64(_request.sessionId)));
	_session->wallet().prepareTonConnectEvent(
		{ .grant = _grant },
		std::move(key),
		{
			.challenge = std::move(challenge),
			.eventId = eventId,
			.address = _address,
			.proofDomain = _request.domain,
			.proofPayload = payload,
		},
		crl::guard(this, [=](TonConnectReply reply) {
			prepared(std::move(reply));
		}),
		crl::guard(this, [=](TonConnectKeyError error) {
			prepareFailed(error);
		}));
}

void TonConnectLoginSection::registerFailed(const MTP::Error &error) {
	const auto &type = error.type();
	LOG(("Wallet Error: login wallet.tonConnectRegisterKey failed: %1"
		).arg(type));
	const auto expired = std::array{
		u"TONCONNECT_CLIENT_ID_OCCUPIED"_q,
		u"TONCONNECT_SESSION_CLOSED"_q,
		u"TONCONNECT_SESSION_NOT_FOUND"_q,
	};
	if (MTP::IgnoreError(error)) {
		fail(QString());
	} else if (ranges::contains(expired, type)) {
		fail(tr::lng_wallet_connect_expired(tr::now));
	} else {
		fail(ErrorWithType(tr::lng_wallet_connect_failed(tr::now), type));
	}
}

void TonConnectLoginSection::prepared(TonConnectReply reply) {
	_grant = nullptr;
	if (const auto ready = base::take(_ready)) {
		ready(MTP_inputTonConnectOauthSession(
			MTP_long(_request.sessionId),
			MTP_bytes(reply.challengeAnswer),
			MTP_bytes(reply.body),
			MTP_walletOwnershipProof(
				MTP_int(reply.proofTimestamp),
				MTP_bytes(reply.proofSignature))));
	}
}

void TonConnectLoginSection::prepareFailed(TonConnectKeyError error) {
	if (error == TonConnectKeyError::Locked) {
		_show->showToast(tr::lng_wallet_vault_locked(tr::now));
		fail(QString());
		return;
	} else if (error == TonConnectKeyError::Blocked) {
		refresh();
	}
	fail(tr::lng_wallet_connect_failed(tr::now));
}

void TonConnectLoginSection::fail(const QString &text) {
	stopManifestWait();
	_grant = nullptr;
	_queued = false;
	_ready = nullptr;
	setBusy(false);
	if (!text.isEmpty()) {
		_error->entity()->setText(text);
		_error->show(anim::type::normal);
	}
}

void TonConnectLoginSection::setBusy(bool busy) {
	_busy = busy;
	const auto state = _state.current();
	_toggle->setDisabled(busy
		|| (state != State::Allowed && state != State::Loading));
	if (_busyChanged) {
		_busyChanged(busy);
	}
}

base::weak_ptr<TonConnectLoginSection> AddTonConnectLoginSection(
		not_null<Ui::VerticalLayout*> container,
		TonConnectLoginArgs &&args) {
	const auto inner = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto section = inner->lifetime().make_state<TonConnectLoginSection>(
		inner,
		std::move(args));
	return base::make_weak(section);
}

} // namespace Wallet
