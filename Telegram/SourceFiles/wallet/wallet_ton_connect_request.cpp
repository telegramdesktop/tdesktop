/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_request.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/delayed_activation.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect_request_box.h"
#include "wallet/wallet_unlock.h"
#include "window/window_session_controller.h"

namespace Wallet {
namespace {

using Phase = TonConnectRequestPhase;

constexpr auto kWalletResolveTimeout = 20 * crl::time(1000);
constexpr auto kPublishAttempts = 5;
constexpr auto kPublishRetryDelay = crl::time(1000);
constexpr auto kDeadlineMaxDelay = 24 * 3600 * crl::time(1000);

[[nodiscard]] bool SessionGone(const QString &type) {
	return (type == u"TONCONNECT_SESSION_CLOSED"_q)
		|| (type == u"TONCONNECT_SESSION_NOT_FOUND"_q);
}

[[nodiscard]] bool RequestDropped(const QString &type) {
	const auto list = std::array{
		u"TONCONNECT_BAD_REQUEST_ID"_q,
		u"TONCONNECT_REQUEST_EXPIRED"_q,
		u"TONCONNECT_REQUEST_NOT_FOUND"_q,
		u"TONCONNECT_SESSION_NOT_ACTIVE"_q,
	};
	return SessionGone(type) || ranges::contains(list, type);
}

[[nodiscard]] QString SessionName(const TonConnectSessionInfo *info) {
	return (info && info->manifest)
		? TonConnectManifestName(*info->manifest)
		: QString();
}

[[nodiscard]] QString SessionDomain(const TonConnectSessionInfo *info) {
	return (info && info->manifest)
		? TonConnectHost(info->manifest->url)
		: QString();
}

[[nodiscard]] QString AccessNoticeText(TonConnectAccess access) {
	switch (access) {
	case TonConnectAccess::KeyChanging:
		return tr::lng_wallet_connect_request_key_changing(tr::now);
	case TonConnectAccess::NoCurrentKey:
		return tr::lng_wallet_connect_request_no_key(tr::now);
	case TonConnectAccess::WalletNotReady:
	case TonConnectAccess::Busy:
		return tr::lng_wallet_state_error(tr::now);
	case TonConnectAccess::Allowed:
		return tr::lng_wallet_connect_request_failed(tr::now);
	}
	Unexpected("Access in TON Connect AccessNoticeText.");
}

} // namespace

class TonConnectRequests::Flow final : public base::has_weak_ptr {
public:
	Flow(
		not_null<TonConnectRequests*> owner,
		base::weak_ptr<Window::SessionController> controller,
		std::shared_ptr<Main::SessionShow> show,
		Entry entry);
	~Flow();

	void start();
	void activate();
	void editedElsewhere();
	[[nodiscard]] bool matches(MsgId msgId) const;

private:
	friend class TonConnectRequests;

	enum class Decision : uchar {
		None,
		Confirm,
		Decline,
		Invalid,
	};

	void fetch();
	void fetched(const MTPwallet_TonConnectPending &result);
	void fetchFailed(const MTP::Error &error);
	void resolve();
	void resolveTimeout();
	void stopResolving();
	void keyNeeded();
	void locked();
	void unlockPressed();
	void keyReady(TonConnectKeyResult result);
	void decrypt();
	void decrypted(TonConnectAppRequest request);
	void preview();
	void previewed(FeeResult result);
	void confirmPressed();
	void confirmKeyReady(TonConnectKeyResult result);
	void declinePressed();
	void answerInvalid();
	void encryptRefusal(TonConnectError error);
	void registerKey();
	void registered(const QByteArray &challenge);
	void registerFailed(const MTP::Error &error);
	void claim(const QByteArray &answer);
	void claimed(const MTPBool &result);
	void claimFailed(const MTP::Error &error);
	void send();
	void sent(TonConnectSendResult result);
	void publish();
	void published(const MTPBool &result);
	void publishFailed(const QString &type);
	void decisionFailed();
	void armDeadline(std::optional<TimeId> validUntil);
	void expired();
	void unavailable();
	void accessNotice(TonConnectAccess access);
	void notice(const QString &text);
	void closeWithToast(const QString &text);
	void backToConfirm(const QString &error);
	void dismissed();
	void closeBox();
	void finish();
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] bool claiming() const;
	[[nodiscard]] std::shared_ptr<Main::SessionShow> showNow() const;

	const not_null<TonConnectRequests*> _owner;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Window::SessionController> _controller;
	const TonConnectSessionId _sessionId = 0;
	const MsgId _msgId = 0;
	const bool _chosen = false;
	MTP::Sender _api;
	std::shared_ptr<Main::SessionShow> _show;
	base::weak_qptr<Ui::GenericBox> _box;
	rpl::variable<TonConnectRequestBoxState> _state;
	QString _topic;
	QString _traceId;
	QByteArray _body;
	TonConnectKey _key;
	TonConnectAppRequest _request;
	std::shared_ptr<const PreparedSend> _prepared;
	KeyAuthorization _auth;
	QByteArray _response;
	TimeId _expires = 0;
	uint64 _previewOwner = 0;
	int _publishAttempts = 0;
	Decision _decision = Decision::None;
	bool _claimSent = false;
	bool _claimed = false;
	bool _sendStarted = false;
	bool _sentBoc = false;
	bool _retriedChallenge = false;
	bool _polling = false;
	bool _closingBox = false;
	bool _terminal = false;
	bool _finished = false;
	base::Timer _deadlineTimer;
	base::Timer _resolveTimer;
	base::Timer _publishTimer;
	rpl::lifetime _resolveLifetime;
	rpl::lifetime _previewLifetime;
	rpl::lifetime _idleLifetime;
	rpl::lifetime _lifetime;

};

TonConnectRequests::TonConnectRequests(
	not_null<Main::Session*> session,
	not_null<TonConnect*> store)
: _session(session)
, _store(store)
, _api(&session->mtp()) {
	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		arrived(item);
	}, _lifetime);

	_session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		edited(update.item);
	}, _lifetime);
}

TonConnectRequests::~TonConnectRequests() = default;

std::optional<TonConnectRequests::Entry> TonConnectRequests::PendingEntry(
		not_null<HistoryItem*> item) {
	if (item->history()->peer->id != PeerData::kServiceNotificationsId) {
		return std::nullopt;
	}
	const auto request = item->Get<HistoryServiceTonConnectRequest>();
	if (!request
		|| request->accepted
		|| request->declined
		|| request->expires <= base::unixtime::now()) {
		return std::nullopt;
	}
	return Entry{
		.sessionId = request->sessionId,
		.msgId = item->id,
		.topic = request->topic,
		.expires = request->expires,
	};
}

bool TonConnectRequests::OpensByItself(const Entry &entry) {
	return (entry.topic != u"disconnect"_q);
}

Window::SessionController *TonConnectRequests::autoWindow() const {
	const auto &windows = _session->windows();
	for (const auto &window : windows) {
		if (window->isPrimary()) {
			return window.get();
		}
	}
	return windows.empty() ? nullptr : windows.front().get();
}

void TonConnectRequests::arrived(not_null<HistoryItem*> item) {
	if (_stopped) {
		return;
	}
	auto entry = PendingEntry(item);
	if (!entry
		|| !OpensByItself(*entry)
		|| _claimedIds.contains(entry->msgId)
		|| (_active && _active->matches(entry->msgId))
		|| ranges::contains(_waiting, entry->msgId, &Entry::msgId)) {
		return;
	}
	entry->order = ++_order;
	_waiting.push_back(std::move(*entry));
	crl::on_main(this, [=] { showNext(); });
}

void TonConnectRequests::edited(not_null<HistoryItem*> item) {
	if (item->history()->peer->id != PeerData::kServiceNotificationsId) {
		return;
	}
	const auto request = item->Get<HistoryServiceTonConnectRequest>();
	if (!request || (!request->accepted && !request->declined)) {
		return;
	}
	const auto i = ranges::find(_waiting, item->id, &Entry::msgId);
	if (i != end(_waiting)) {
		_waiting.erase(i);
	}
	if (_active && _active->matches(item->id)) {
		_active->editedElsewhere();
	}
}

void TonConnectRequests::showNext() {
	if (_stopped || _active) {
		return;
	}
	const auto now = base::unixtime::now();
	_waiting.erase(ranges::remove_if(_waiting, [&](const Entry &entry) {
		return (entry.expires <= now) || _claimedIds.contains(entry.msgId);
	}), end(_waiting));
	if (_waiting.empty()) {
		return;
	}
	const auto window = autoWindow();
	if (!window) {
		return;
	}
	auto i = ranges::min_element(_waiting, ranges::less(), &Entry::order);
	if (!i->chosen) {
		const auto sessionId = i->sessionId;
		for (auto j = begin(_waiting); j != end(_waiting); ++j) {
			if ((j->sessionId == sessionId) && (j->msgId < i->msgId)) {
				i = j;
			}
		}
	}
	auto entry = std::move(*i);
	_waiting.erase(i);
	start(
		std::move(entry),
		base::make_weak(window),
		TonConnectBoxShowNoActivate(window));
}

void TonConnectRequests::open(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (_stopped || itemId.peer != PeerData::kServiceNotificationsId) {
		return;
	}
	const auto item = _session->data().message(itemId);
	if (!item) {
		return;
	}
	const auto entry = PendingEntry(item);
	if (!entry || _claimedIds.contains(entry->msgId)) {
		return;
	}
	opened(*entry, controller);
}

void TonConnectRequests::opened(
		Entry entry,
		not_null<Window::SessionController*> controller) {
	entry.chosen = true;
	if (_active && _active->matches(entry.msgId)) {
		_active->activate();
		return;
	}
	const auto i = ranges::find(_waiting, entry.msgId, &Entry::msgId);
	if (i != end(_waiting)) {
		_waiting.erase(i);
	}
	if (_active) {
		entry.order = 0;
		_waiting.insert(begin(_waiting), std::move(entry));
		_active->activate();
		return;
	}
	start(
		std::move(entry),
		base::make_weak(controller),
		TonConnectBoxShow(controller));
}

void TonConnectRequests::openPending(
		not_null<Window::SessionController*> controller,
		const QString &dappClientId) {
	if (_stopped) {
		return;
	}
	_api.request(base::take(_pendingRequestId)).cancel();
	const auto weak = base::make_weak(controller);
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_pendingRequestId = _api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_dapp_client_id),
		MTP_string(dappClientId),
		MTPlong()
	)).done([=](const MTPwallet_TonConnectPending &result) {
		_pendingRequestId = 0;
		pendingLoaded(weak, result);
	}).fail([=](const MTP::Error &error) {
		_pendingRequestId = 0;
		const auto &type = error.type();
		if (type != u"TONCONNECT_SESSION_NOT_FOUND"_q) {
			LOG(("Wallet Error: wallet.tonConnectGetPending failed: %1"
				).arg(type));
		}
	}).send();
}

void TonConnectRequests::pendingLoaded(
		base::weak_ptr<Window::SessionController> controller,
		const MTPwallet_TonConnectPending &result) {
	const auto &data = result.data();
	_store->apply(data.vsession());
	if (data.vsession().data().is_closed()) {
		return;
	}
	const auto now = base::unixtime::now();
	auto oldest = std::optional<Entry>();
	for (const auto &request : data.vrequests().v) {
		const auto &fields = request.data();
		const auto msgId = MsgId(fields.vmsg_id().v);
		if (fields.vexpires().v <= now || _claimedIds.contains(msgId)) {
			continue;
		} else if (!oldest || msgId < oldest->msgId) {
			oldest = Entry{
				.sessionId = uint64(fields.vsession_id().v),
				.msgId = msgId,
				.topic = qs(fields.vtopic().value_or_empty()),
				.expires = fields.vexpires().v,
			};
		}
	}
	if (!oldest) {
		ShowWallet(_session);
		return;
	}
	const auto strong = controller.get();
	if (!strong) {
		return;
	}
	opened(std::move(*oldest), strong);
}

void TonConnectRequests::start(
		Entry entry,
		base::weak_ptr<Window::SessionController> controller,
		std::shared_ptr<Main::SessionShow> show) {
	_active = std::make_unique<Flow>(
		this,
		std::move(controller),
		std::move(show),
		std::move(entry));
	_active->start();
}

void TonConnectRequests::flowDone(not_null<Flow*> flow, bool claimed) {
	const auto msgId = flow->_msgId;
	crl::on_main(this, [=] {
		if (_active.get() != flow) {
			return;
		} else if (claimed) {
			_claimedIds.emplace(msgId);
		}
		_active = nullptr;
		showNext();
	});
}

void TonConnectRequests::stop() {
	_stopped = true;
	_waiting.clear();
	_api.request(base::take(_pendingRequestId)).cancel();
	_active = nullptr;
	_lifetime.destroy();
}

TonConnectRequests::Flow::Flow(
	not_null<TonConnectRequests*> owner,
	base::weak_ptr<Window::SessionController> controller,
	std::shared_ptr<Main::SessionShow> show,
	Entry entry)
: _owner(owner)
, _session(owner->_session)
, _controller(std::move(controller))
, _sessionId(entry.sessionId)
, _msgId(entry.msgId)
, _chosen(entry.chosen)
, _api(&_session->mtp())
, _show(std::move(show))
, _topic(entry.topic)
, _expires(entry.expires)
, _deadlineTimer([=] { expired(); })
, _resolveTimer([=] { resolveTimeout(); })
, _publishTimer([=] { publish(); }) {
}

TonConnectRequests::Flow::~Flow() {
	closeBox();
}

void TonConnectRequests::Flow::start() {
	const auto info = _owner->_store->session(_sessionId);
	_state = TonConnectRequestBoxState{
		.phase = Phase::Loading,
		.name = SessionName(info),
		.domain = SessionDomain(info),
	};
	auto box = Box(TonConnectRequestBox, TonConnectRequestBoxArgs{
		.session = _session,
		.state = _state.value(),
		.unlock = crl::guard(this, [=] { unlockPressed(); }),
		.confirm = crl::guard(this, [=] { confirmPressed(); }),
		.decline = crl::guard(this, [=] { declinePressed(); }),
		.dismissed = crl::guard(this, [=] { dismissed(); }),
	});
	_box = box.data();
	_show->showBox(std::move(box));
	if (!_box) {
		finish();
		return;
	}
	armDeadline(std::nullopt);
	fetch();
}

void TonConnectRequests::Flow::activate() {
	if (const auto box = _box.get()) {
		Ui::ActivateWindow(box->window());
	}
}

void TonConnectRequests::Flow::editedElsewhere() {
	if (claiming() || stopped()) {
		return;
	}
	closeWithToast(tr::lng_wallet_connect_request_handled(tr::now));
}

bool TonConnectRequests::Flow::matches(MsgId msgId) const {
	return (_msgId == msgId);
}

void TonConnectRequests::Flow::fetch() {
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_session_id),
		MTPstring(),
		MTP_long(_sessionId)
	)).done([=](const MTPwallet_TonConnectPending &result) {
		fetched(result);
	}).fail([=](const MTP::Error &error) {
		fetchFailed(error);
	}).send();
}

void TonConnectRequests::Flow::fetched(
		const MTPwallet_TonConnectPending &result) {
	if (stopped()) {
		return;
	}
	const auto &data = result.data();
	_owner->_store->apply(data.vsession());
	const auto &session = data.vsession().data();
	if (session.is_closed()) {
		if (_chosen) {
			closeWithToast(tr::lng_wallet_connect_request_closed(tr::now));
		} else {
			_terminal = true;
			closeBox();
			finish();
		}
		return;
	} else if (session.is_pending()) {
		unavailable();
		return;
	}
	const auto now = base::unixtime::now();
	for (const auto &request : data.vrequests().v) {
		const auto &fields = request.data();
		if (fields.vmsg_id().v != _msgId.bare) {
			continue;
		}
		_body = fields.vbody().v;
		_expires = fields.vexpires().v;
		if (const auto topic = fields.vtopic()) {
			_topic = qs(*topic);
		}
		_traceId = qs(fields.vtrace_id().value_or_empty());
		const auto info = _owner->_store->session(_sessionId);
		auto state = _state.current();
		state.name = SessionName(info);
		state.domain = SessionDomain(info);
		_state = std::move(state);
		armDeadline(std::nullopt);

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
		return;
	}
	const auto item = _session->data().message(
		FullMsgId(PeerData::kServiceNotificationsId, _msgId));
	const auto loaded = item
		? item->Get<HistoryServiceTonConnectRequest>()
		: nullptr;
	if (loaded && (loaded->accepted || loaded->declined)) {
		closeWithToast(tr::lng_wallet_connect_request_handled(tr::now));
	} else if (_expires <= now) {
		expired();
	} else {
		unavailable();
	}
}

void TonConnectRequests::Flow::fetchFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	if (SessionGone(type)) {
		unavailable();
		return;
	}
	LOG(("Wallet Error: wallet.tonConnectGetPending failed: %1"
		).arg(type));
	notice(tr::lng_wallet_connect_request_failed(tr::now));
}

void TonConnectRequests::Flow::resolve() {
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
	if (presence == Presence::Ready) {
		keyNeeded();
	} else {
		notice(tr::lng_wallet_state_error(tr::now));
	}
}

void TonConnectRequests::Flow::resolveTimeout() {
	if (!stopped()) {
		notice(tr::lng_wallet_state_error(tr::now));
	}
}

void TonConnectRequests::Flow::stopResolving() {
	_resolveTimer.cancel();
	_resolveLifetime.destroy();
	if (base::take(_polling)) {
		_session->wallet().stopPolling();
	}
}

void TonConnectRequests::Flow::keyNeeded() {
	auto &wallet = _session->wallet();
	if (!_lifetime) {
		wallet.transferWalletIdentityChanges(
		) | rpl::on_next([=] {
			if (!claiming()) {
				unavailable();
			}
		}, _lifetime);
	}
	const auto access = wallet.tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	}
	auto cached = _owner->_store->key(_sessionId);
	if (cached) {
		_key = std::move(cached);
		decrypt();
	} else if (VaultUnlockSilent(_session)) {
		_owner->_store->acquireKey(
			showNow(),
			_sessionId,
			false,
			crl::guard(this, [=](TonConnectKeyResult result) {
				keyReady(std::move(result));
			}));
	} else {
		locked();
	}
}

void TonConnectRequests::Flow::locked() {
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Locked,
		.name = current.name,
		.domain = current.domain,
		.topic = TonConnectRequestText(_topic, current.name).text,
	};
}

void TonConnectRequests::Flow::unlockPressed() {
	auto state = _state.current();
	if (stopped() || state.phase != Phase::Locked || state.busy) {
		return;
	}
	state.busy = true;
	_state = std::move(state);
	_owner->_store->acquireKey(
		showNow(),
		_sessionId,
		false,
		crl::guard(this, [=](TonConnectKeyResult result) {
			keyReady(std::move(result));
		}));
}

void TonConnectRequests::Flow::keyReady(TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	if (stopped()) {
		return;
	}
	switch (result.error) {
	case Error::None:
		_key = std::move(result.key);
		decrypt();
		return;
	case Error::Cancelled:
		locked();
		return;
	case Error::Locked:
		if (_box) {
			showNow()->showToast(VaultLockedText(_session));
		}
		locked();
		return;
	case Error::Blocked:
		accessNotice(_session->wallet().tonConnectAccess());
		return;
	case Error::OtherKey:
		notice(tr::lng_wallet_connect_request_other_key(tr::now));
		return;
	case Error::Failed:
		notice(tr::lng_wallet_connect_request_failed(tr::now));
		return;
	}
	Unexpected("Error in TonConnectRequests::Flow::keyReady.");
}

void TonConnectRequests::Flow::decrypt() {
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Loading,
		.name = current.name,
		.domain = current.domain,
	};
	_session->wallet().decryptTonConnectRequest(
		_key,
		_body,
		crl::guard(this, [=](TonConnectAppRequest request) {
			decrypted(std::move(request));
		}),
		crl::guard(this, [=] { unavailable(); }));
}

void TonConnectRequests::Flow::decrypted(TonConnectAppRequest request) {
	using Kind = TonConnectRequestKind;
	if (stopped()) {
		return;
	}
	_request = std::move(request);
	switch (_request.kind) {
	case Kind::Disconnect:
		_terminal = true;
		closeBox();
		finish();
		return;
	case Kind::Unsupported:
		notice(tr::lng_wallet_connect_request_unsupported(tr::now));
		return;
	case Kind::Invalid:
	case Kind::SendTransaction:
		break;
	}
	const auto transfer = _request.transfer;
	if (!_request.appRequestId
		|| (_request.kind == Kind::SendTransaction && !transfer)) {
		unavailable();
	} else if (_request.kind == Kind::Invalid) {
		answerInvalid();
	} else {
		armDeadline(transfer->validUntil);
		preview();
	}
}

void TonConnectRequests::Flow::preview() {
	auto &wallet = _session->wallet();
	if (!_previewOwner) {
		_previewOwner = wallet.createPreviewOwner(_previewLifetime);
	}
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Confirm,
		.name = current.name,
		.domain = current.domain,
		.transfer = _request.transfer,
		.feeLoading = true,
	};
	wallet.estimateTonConnect(
		_previewOwner,
		_request.transfer,
		crl::guard(this, [=](FeeResult result) {
			previewed(std::move(result));
		}));
}

void TonConnectRequests::Flow::previewed(FeeResult result) {
	if (stopped() || _decision != Decision::None) {
		return;
	}
	auto state = _state.current();
	state.feeLoading = false;
	if (result.error == SendError::None && result.prepared) {
		_prepared = std::move(result.prepared);
		state.feeNano = result.feeNano;
		state.confirmable = true;
		state.error = QString();
		_state = std::move(state);
		return;
	}
	_prepared = nullptr;
	state.confirmable = false;
	const auto text = SendErrorText(result.error, TransferMinNanos(_session));
	state.error = text.isEmpty()
		? tr::lng_wallet_connect_request_fee_failed(tr::now)
		: text;
	_state = std::move(state);
	if (result.error == SendError::PreviousUnresolved
		|| result.error == SendError::AlreadySending) {
		_idleLifetime.destroy();
		_session->wallet().sendStateValue(
		) | rpl::skip(1) | rpl::take(1) | rpl::on_next([=](SendState) {
			if (_decision == Decision::None && !stopped()) {
				preview();
			}
		}, _idleLifetime);
	}
}

void TonConnectRequests::Flow::confirmPressed() {
	auto state = _state.current();
	if (stopped()
		|| _decision != Decision::None
		|| state.phase != Phase::Confirm
		|| !_prepared
		|| state.busy) {
		return;
	}
	_decision = Decision::Confirm;
	state.busy = true;
	state.declining = false;
	state.error = QString();
	_state = std::move(state);
	_owner->_store->acquireKey(
		showNow(),
		_sessionId,
		true,
		crl::guard(this, [=](TonConnectKeyResult result) {
			confirmKeyReady(std::move(result));
		}));
}

void TonConnectRequests::Flow::confirmKeyReady(TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	if (stopped()) {
		return;
	}
	auto &wallet = _session->wallet();
	switch (result.error) {
	case Error::None:
		break;
	case Error::Cancelled:
		backToConfirm(QString());
		return;
	case Error::Locked:
		if (_box) {
			showNow()->showToast(VaultLockedText(_session));
		}
		backToConfirm(QString());
		return;
	case Error::Blocked:
		accessNotice(wallet.tonConnectAccess());
		return;
	case Error::OtherKey:
		notice(tr::lng_wallet_connect_request_other_key(tr::now));
		return;
	case Error::Failed:
		backToConfirm(tr::lng_wallet_connect_request_failed(tr::now));
		return;
	}
	_key = std::move(result.key);
	_auth = KeyAuthorization{ .grant = std::move(result.grant) };
	const auto access = wallet.tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	}
	const auto refusal = wallet.sendRefusal(_prepared, _auth);
	if (refusal != SendError::None) {
		backToConfirm(SendErrorText(refusal, TransferMinNanos(_session)));
		return;
	}
	registerKey();
}

void TonConnectRequests::Flow::declinePressed() {
	auto state = _state.current();
	if (stopped()
		|| _decision != Decision::None
		|| state.phase != Phase::Confirm
		|| state.busy) {
		return;
	}
	_decision = Decision::Decline;
	state.busy = true;
	state.declining = true;
	_state = std::move(state);
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	}
	encryptRefusal(TonConnectError::UserDeclined);
}

void TonConnectRequests::Flow::answerInvalid() {
	_decision = Decision::Invalid;
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	}
	encryptRefusal(TonConnectError::BadRequest);
}

void TonConnectRequests::Flow::encryptRefusal(TonConnectError error) {
	_session->wallet().encryptTonConnectResponse(
		_key,
		_request.id,
		{ .error = error },
		crl::guard(this, [=](QByteArray body) {
			if (!stopped()) {
				_response = std::move(body);
				registerKey();
			}
		}),
		crl::guard(this, [=] {
			if (!stopped()) {
				decisionFailed();
			}
		}));
}

void TonConnectRequests::Flow::registerKey() {
	if (!_key) {
		decisionFailed();
		return;
	}
	_api.request(MTPwallet_TonConnectRegisterKey(
		MTP_long(_sessionId),
		MTP_string(_key.clientId)
	)).done([=](const MTPwallet_TonConnectChallenge &result) {
		registered(result.data().vchallenge().v);
	}).fail([=](const MTP::Error &error) {
		registerFailed(error);
	}).send();
}

void TonConnectRequests::Flow::registered(const QByteArray &challenge) {
	if (stopped()) {
		return;
	}
	_session->wallet().answerTonConnectChallenge(
		_key,
		challenge,
		crl::guard(this, [=](QByteArray answer) {
			claim(answer);
		}),
		crl::guard(this, [=] {
			if (!stopped()) {
				decisionFailed();
			}
		}));
}

void TonConnectRequests::Flow::registerFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	LOG(("Wallet Error: wallet.tonConnectRegisterKey failed: %1"
		).arg(type));
	if (type == u"TONCONNECT_CLIENT_ID_OCCUPIED"_q) {
		notice(tr::lng_wallet_connect_request_other_key(tr::now));
	} else if (SessionGone(type)) {
		unavailable();
	} else {
		decisionFailed();
	}
}

void TonConnectRequests::Flow::claim(const QByteArray &answer) {
	if (stopped() || _claimSent) {
		return;
	} else if (base::unixtime::now() >= _expires) {
		expired();
		return;
	}
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	} else if (!_request.appRequestId) {
		unavailable();
		return;
	}
	_claimSent = true;
	using Flag = MTPwallet_TonConnectClaimRequest::Flag;
	_api.request(MTPwallet_TonConnectClaimRequest(
		MTP_flags(Flag::f_challenge_answer
			| ((_decision == Decision::Confirm)
				? Flag(0)
				: Flag::f_declined)),
		MTP_long(_sessionId),
		MTP_long(_msgId.bare),
		MTP_long(*_request.appRequestId),
		MTP_bytes(answer)
	)).done([=](const MTPBool &result) {
		claimed(result);
	}).fail([=](const MTP::Error &error) {
		claimFailed(error);
	}).handleAllErrors().send();
}

void TonConnectRequests::Flow::claimed(const MTPBool &result) {
	if (stopped()) {
		return;
	} else if (!mtpIsTrue(result)) {
		_claimSent = false;
		unavailable();
		return;
	}
	_claimed = true;
	if (_decision == Decision::Confirm) {
		send();
	} else {
		publish();
	}
}

void TonConnectRequests::Flow::claimFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	const auto code = error.code();
	LOG(("Wallet Error: wallet.tonConnectClaimRequest failed: %1 (%2)"
		).arg(type).arg(code));
	if (code < 400 || code >= 500) {
		closeWithToast(tr::lng_wallet_connect_request_failed(tr::now));
		return;
	}
	// WHY: the server records a declined claim but answers
	// MESSAGE_NOT_MODIFIED, its edit of the request message changing nothing;
	// the request is ours to answer, and a refusal signs nothing.
	if (type == u"MESSAGE_NOT_MODIFIED"_q && _decision != Decision::Confirm) {
		_claimed = true;
		publish();
		return;
	}
	_claimSent = false;
	if (type == u"TONCONNECT_REQUEST_ALREADY_CLAIMED"_q) {
		closeWithToast(tr::lng_wallet_connect_request_handled(tr::now));
	} else if (RequestDropped(type)) {
		unavailable();
	} else if (type == u"TONCONNECT_CHALLENGE_INVALID"_q
		&& !_retriedChallenge) {
		_retriedChallenge = true;
		registerKey();
	} else {
		decisionFailed();
	}
}

void TonConnectRequests::Flow::send() {
	if (_sendStarted) {
		return;
	}
	_sendStarted = true;
	_session->wallet().sendTonConnect(
		base::take(_auth),
		_prepared,
		crl::guard(this, [=](TonConnectSendResult result) {
			sent(std::move(result));
		}));
}

void TonConnectRequests::Flow::sent(TonConnectSendResult result) {
	if (stopped()) {
		return;
	}
	_sentBoc = !result.signedBoc.isEmpty();
	auto &wallet = _session->wallet();
	const auto done = crl::guard(this, [=](QByteArray body) {
		if (!stopped()) {
			_response = std::move(body);
			publish();
		}
	});
	const auto undelivered = crl::guard(this, [=] {
		if (stopped()) {
			return;
		}
		LOG(("Wallet Error: TON Connect answer could not be encrypted."));
		closeWithToast(tr::lng_wallet_connect_request_undelivered(tr::now));
	});
	if (!_sentBoc) {
		wallet.encryptTonConnectResponse(
			_key,
			_request.id,
			{ .error = TonConnectError::Unknown },
			done,
			undelivered);
		return;
	}
	wallet.encryptTonConnectResponse(
		_key,
		_request.id,
		{ .signedBoc = result.signedBoc },
		done,
		crl::guard(this, [=] {
			if (stopped()) {
				return;
			}
			LOG(("Wallet Error: TON Connect success answer could not "
				"be encrypted, answering an error."));
			_session->wallet().encryptTonConnectResponse(
				_key,
				_request.id,
				{ .error = TonConnectError::Unknown },
				done,
				undelivered);
		}));
}

void TonConnectRequests::Flow::publish() {
	if (stopped()) {
		return;
	}
	++_publishAttempts;
	using Flag = MTPwallet_TonConnectSubmitResponse::Flag;
	_api.request(MTPwallet_TonConnectSubmitResponse(
		MTP_flags(_traceId.isEmpty() ? Flag(0) : Flag::f_trace_id),
		MTP_long(_sessionId),
		MTP_long(_msgId.bare),
		MTP_bytes(_response),
		MTP_string(_traceId)
	)).done([=](const MTPBool &result) {
		published(result);
	}).fail([=](const MTP::Error &error) {
		publishFailed(error.type());
	}).send();
}

void TonConnectRequests::Flow::published(const MTPBool &result) {
	if (stopped()) {
		return;
	} else if (!mtpIsTrue(result)) {
		publishFailed(QString());
		return;
	}
	switch (_decision) {
	case Decision::Confirm:
		closeWithToast(_sentBoc
			? tr::lng_wallet_connect_request_sent(tr::now)
			: tr::lng_wallet_connect_request_not_sent(tr::now));
		return;
	case Decision::Invalid:
		notice(tr::lng_wallet_connect_request_invalid(tr::now));
		return;
	case Decision::Decline:
	case Decision::None:
		_terminal = true;
		closeBox();
		finish();
		return;
	}
	Unexpected("Decision in TonConnectRequests::Flow::published.");
}

void TonConnectRequests::Flow::publishFailed(const QString &type) {
	if (stopped()) {
		return;
	}
	LOG(("Wallet Error: wallet.tonConnectSubmitResponse failed: %1"
		).arg(type.isEmpty() ? u"FALSE"_q : type));
	if (type == u"TONCONNECT_PUBLISH_FAILED"_q
		&& _publishAttempts < kPublishAttempts) {
		_publishTimer.callOnce(
			kPublishRetryDelay * (crl::time(1) << (_publishAttempts - 1)));
	} else {
		closeWithToast(tr::lng_wallet_connect_request_undelivered(tr::now));
	}
}

void TonConnectRequests::Flow::decisionFailed() {
	if (_decision == Decision::Invalid) {
		closeWithToast(tr::lng_wallet_connect_request_failed(tr::now));
	} else {
		backToConfirm(tr::lng_wallet_connect_request_failed(tr::now));
	}
}

void TonConnectRequests::Flow::armDeadline(std::optional<TimeId> validUntil) {
	const auto deadline = validUntil
		? std::min(_expires, *validUntil)
		: _expires;
	const auto left = std::max(
		crl::time(deadline) - base::unixtime::now(),
		crl::time(0));
	_deadlineTimer.callOnce(
		std::min(left * 1000, kDeadlineMaxDelay),
		Qt::PreciseTimer);
}

void TonConnectRequests::Flow::expired() {
	if (claiming()) {
		return;
	}
	closeWithToast(tr::lng_wallet_connect_request_expired(tr::now));
}

void TonConnectRequests::Flow::unavailable() {
	closeWithToast(tr::lng_wallet_connect_request_unavailable(tr::now));
}

void TonConnectRequests::Flow::accessNotice(TonConnectAccess access) {
	notice(AccessNoticeText(access));
}

void TonConnectRequests::Flow::notice(const QString &text) {
	_terminal = true;
	_auth = KeyAuthorization();
	stopResolving();
	_deadlineTimer.cancel();
	_publishTimer.cancel();
	if (!_box) {
		finish();
		return;
	}
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Notice,
		.name = current.name,
		.domain = current.domain,
		.notice = text,
	};
}

void TonConnectRequests::Flow::closeWithToast(const QString &text) {
	if (stopped()) {
		return;
	}
	_terminal = true;
	const auto show = showNow();
	closeBox();
	if (show->valid()) {
		show->showToast(text);
	}
	finish();
}

void TonConnectRequests::Flow::backToConfirm(const QString &error) {
	_decision = Decision::None;
	_retriedChallenge = false;
	_auth = KeyAuthorization();
	_response = QByteArray();
	if (!_box) {
		finish();
		return;
	}
	auto state = _state.current();
	state.phase = Phase::Confirm;
	state.busy = false;
	state.declining = false;
	state.error = error;
	_state = std::move(state);
}

void TonConnectRequests::Flow::dismissed() {
	if (_closingBox || _finished) {
		return;
	}
	_box.reset();
	if (_decision != Decision::None && !_terminal) {
		return;
	}
	finish();
}

void TonConnectRequests::Flow::closeBox() {
	_closingBox = true;
	if (const auto box = _box.get()) {
		_box.reset();
		if (box->hasDelegate()) {
			box->closeBox();
		}
	}
}

void TonConnectRequests::Flow::finish() {
	if (std::exchange(_finished, true)) {
		return;
	}
	_auth = KeyAuthorization();
	stopResolving();
	_deadlineTimer.cancel();
	_publishTimer.cancel();
	_previewLifetime.destroy();
	_idleLifetime.destroy();
	_lifetime.destroy();
	_owner->flowDone(this, claiming());
}

bool TonConnectRequests::Flow::stopped() const {
	return _terminal || _finished;
}

bool TonConnectRequests::Flow::claiming() const {
	return _claimSent || _claimed;
}

std::shared_ptr<Main::SessionShow> TonConnectRequests::Flow::showNow() const {
	if (_show->valid()) {
		return _show;
	} else if (const auto controller = _controller.get()) {
		return TonConnectBoxShowNoActivate(controller);
	}
	return _show;
}

} // namespace Wallet
