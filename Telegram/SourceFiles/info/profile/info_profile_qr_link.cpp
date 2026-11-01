/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_qr_link.h"

#include "api/api_user_privacy.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <QtCore/QDateTime>

namespace Info::Profile {
namespace {

using Privacy = Api::UserPrivacy;

class OwnQrLinkState final : public base::has_weak_ptr {
public:
	OwnQrLinkState(
		not_null<Main::Session*> session,
		Fn<void(OwnQrLink)> next,
		Fn<void(QString)> fail);
	~OwnQrLinkState();

	void start();

private:
	void refresh();
	void leaveContact();
	void requestContact();
	void sendContactRequest(uint64 generation, mtpRequestId requestId);
	void applyContact(
		uint64 generation,
		mtpRequestId requestId,
		const MTPExportedContactToken &result);
	void failContact(
		uint64 generation,
		mtpRequestId requestId,
		QString error);
	void tick();
	void scheduleTick();
	void publish(OwnQrLink value);
	[[nodiscard]] bool accepts(
		uint64 generation,
		mtpRequestId requestId) const;

	const not_null<Main::Session*> _session;
	const not_null<UserData*> _self;
	const Fn<void(OwnQrLink)> _next;
	const Fn<void(QString)> _fail;
	MTP::Sender _api;
	base::Timer _timer;
	QString _username;
	QString _phoneLabel;
	std::optional<Privacy::Option> _phonePrivacy;
	std::optional<Privacy::Option> _addedPrivacy;
	OwnQrLink _current;
	mtpRequestId _requestId = 0;
	uint64 _generation = 0;
	bool _started = false;
	bool _contact = false;
	bool _failed = false;
	bool _published = false;
	rpl::lifetime _lifetime;

};

[[nodiscard]] QString CountdownText(TimeId remaining) {
	return u"%1:%2"_q
		.arg(remaining / 60)
		.arg(remaining % 60, 2, 10, QChar('0'));
}

[[nodiscard]] QString ErrorText(const MTP::Error &error) {
	return MTP::IgnoreError(error)
		? QString()
		: MTP::IsFloodError(error)
		? tr::lng_flood_error(tr::now)
		: error.type();
}

OwnQrLinkState::OwnQrLinkState(
	not_null<Main::Session*> session,
	Fn<void(OwnQrLink)> next,
	Fn<void(QString)> fail)
: _session(session)
, _self(session->user())
, _next(std::move(next))
, _fail(std::move(fail))
, _api(&session->mtp())
, _timer([=] { tick(); }) {
}

OwnQrLinkState::~OwnQrLinkState() {
	invalidate_weak_ptrs(this);
}

void OwnQrLinkState::start() {
	UsernameValue(_self, true) | rpl::on_next([=](
			const TextWithEntities &username) {
		_username = username.text;
		refresh();
	}, _lifetime);
	PhoneValue(_self) | rpl::on_next([=](const TextWithEntities &phone) {
		_phoneLabel = phone.text;
		refresh();
	}, _lifetime);
	auto &privacy = _session->api().userPrivacy();
	privacy.value(Privacy::Key::PhoneNumber) | rpl::on_next([=](
			const Privacy::Rule &rule) {
		_phonePrivacy = rule.option;
		refresh();
	}, _lifetime);
	privacy.value(Privacy::Key::AddedByPhone) | rpl::on_next([=](
			const Privacy::Rule &rule) {
		_addedPrivacy = rule.option;
		refresh();
	}, _lifetime);
	base::unixtime::updates() | rpl::on_next([=] {
		tick();
	}, _lifetime);
	privacy.reload(Privacy::Key::PhoneNumber);
	privacy.reload(Privacy::Key::AddedByPhone);
	_started = true;
	refresh();
}

void OwnQrLinkState::refresh() {
	if (!_started) {
		return;
	} else if (!_username.isEmpty()) {
		leaveContact();
		publish({
			.kind = OwnQrLinkKind::Username,
			.link = _session->createInternalLinkFull(_username.mid(1)),
			.label = _username.toUpper(),
		});
	} else if (_phonePrivacy
		&& _addedPrivacy
		&& !_self->phone().isEmpty()
		&& !(*_phonePrivacy == Privacy::Option::Nobody
			&& *_addedPrivacy != Privacy::Option::Everyone)) {
		leaveContact();
		publish({
			.kind = OwnQrLinkKind::Phone,
			.link = PublicLinkByPhone(_self),
			.label = _phoneLabel,
		});
	} else if (!_contact) {
		_contact = true;
		_failed = false;
		requestContact();
	}
}

void OwnQrLinkState::leaveContact() {
	if (!_contact) {
		return;
	}
	_contact = false;
	_failed = false;
	++_generation;
	_timer.cancel();
	_api.request(base::take(_requestId)).cancel();
}

void OwnQrLinkState::requestContact() {
	if (!_contact || _requestId || _failed) {
		return;
	}
	_timer.cancel();
	const auto generation = ++_generation;
	const auto requestId = (_requestId = _api.allocateRequestId());
	const auto weak = base::make_weak(this);
	publish({});
	if (const auto state = weak.get()) {
		if (state->accepts(generation, requestId)) {
			state->sendContactRequest(generation, requestId);
		}
	}
}

void OwnQrLinkState::sendContactRequest(
		uint64 generation,
		mtpRequestId requestId) {
	_api.request(MTPcontacts_ExportContactToken(
	)).overrideId(requestId).done(crl::guard(this, [=](
			const MTPExportedContactToken &result) {
		applyContact(generation, requestId, result);
	})).fail(crl::guard(this, [=](const MTP::Error &error) {
		failContact(generation, requestId, ErrorText(error));
	})).handleAllErrors().send();
}

void OwnQrLinkState::applyContact(
		uint64 generation,
		mtpRequestId requestId,
		const MTPExportedContactToken &result) {
	if (!accepts(generation, requestId)) {
		return;
	}
	const auto &data = result.data();
	const auto link = qs(data.vurl());
	const auto expires = TimeId(data.vexpires().v);
	const auto remaining = expires - base::unixtime::now();
	if (link.isEmpty() || remaining <= 0) {
		failContact(
			generation,
			requestId,
			tr::lng_peer_qr_link_unavailable(tr::now));
		return;
	}
	_requestId = 0;
	scheduleTick();
	publish({
		.kind = OwnQrLinkKind::Contact,
		.link = link,
		.label = CountdownText(remaining),
		.expires = expires,
	});
}

void OwnQrLinkState::failContact(
		uint64 generation,
		mtpRequestId requestId,
		QString error) {
	if (!accepts(generation, requestId)) {
		return;
	}
	_requestId = 0;
	_failed = true;
	_timer.cancel();
	const auto fail = _fail;
	if (fail && !error.isEmpty()) {
		fail(std::move(error));
	}
}

void OwnQrLinkState::tick() {
	if (!_contact || _requestId || _failed || _current.link.isEmpty()) {
		return;
	}
	const auto remaining = std::max(
		_current.expires - base::unixtime::now(),
		TimeId(0));
	if (!remaining) {
		requestContact();
		return;
	}
	auto value = _current;
	value.label = CountdownText(remaining);
	scheduleTick();
	publish(std::move(value));
}

void OwnQrLinkState::scheduleTick() {
	_timer.callOnce(
		1000 - (QDateTime::currentMSecsSinceEpoch() % 1000),
		Qt::PreciseTimer);
}

void OwnQrLinkState::publish(OwnQrLink value) {
	if (_published && _current == value) {
		return;
	}
	_current = value;
	_published = true;
	const auto next = _next;
	next(std::move(value));
}

bool OwnQrLinkState::accepts(
		uint64 generation,
		mtpRequestId requestId) const {
	return _contact
		&& (_generation == generation)
		&& (_requestId == requestId);
}

} // namespace

rpl::producer<OwnQrLink> OwnQrLinkValue(
		not_null<Main::Session*> session,
		Fn<void(QString)> fail) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto state = lifetime.make_state<OwnQrLinkState>(
			session,
			[=](OwnQrLink value) { consumer.put_next(std::move(value)); },
			fail);
		state->start();
		return lifetime;
	};
}

} // namespace Info::Profile
