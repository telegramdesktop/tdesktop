/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/passkeys.h"

#include "apiwrap.h"
#include "data/data_passkey_deserialize.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/mtproto_auth_key.h"
#include "platform/platform_webauthn.h"

namespace Data {
namespace {

constexpr auto kRequestTimeout = crl::time(5000);

[[nodiscard]] PasskeyEntry FromTL(const MTPDpasskey &data) {
	return PasskeyEntry{
		.id = qs(data.vid()),
		.name = qs(data.vname()),
		.date = data.vdate().v,
		.softwareEmojiId = data.vsoftware_emoji_id().value_or(0),
		.lastUsageDate = data.vlast_usage_date().value_or(0),
	};
}

} // namespace

Passkeys::Passkeys(not_null<Main::Session*> session)
: _session(session) {
}

Passkeys::~Passkeys() = default;

void Passkeys::initRegistration(
		Fn<void(const Data::Passkey::RegisterData&)> done) {
	if (_pendingRegistration
		&& (crl::now() - _pendingRegistrationTime
			< crl::time(_pendingRegistration->timeout))) {
		done(*_pendingRegistration);
		return;
	}
	_pendingRegistration = nullptr;
	_session->api().request(MTPaccount_InitPasskeyRegistration(
	)).done([=](const MTPaccount_PasskeyRegistrationOptions &result) {
		const auto &data = result.data();
		const auto jsonData = data.voptions().data().vdata().v;
		if (const auto p = Data::Passkey::DeserializeRegisterData(jsonData)) {
			_pendingRegistration
				= std::make_unique<Data::Passkey::RegisterData>(*p);
			_pendingRegistrationTime = crl::now();
			done(*p);
		}
	}).send();
}

void Passkeys::registerPasskey(
		const Platform::WebAuthn::RegisterResult &result,
		Fn<void()> done) {
	const auto credentialIdBase64 = QString::fromUtf8(
		result.credentialId.toBase64(QByteArray::Base64UrlEncoding));
	_session->api().request(MTPaccount_RegisterPasskey(
		MTP_inputPasskeyCredentialPublicKey(
			MTP_string(credentialIdBase64),
			MTP_string(credentialIdBase64),
			MTP_inputPasskeyResponseRegister(
				MTP_dataJSON(MTP_bytes(result.clientDataJSON)),
				MTP_bytes(result.attestationObject)))
	)).done([=](const MTPPasskey &result) {
		_pendingRegistration = nullptr;
		_passkeys.emplace_back(FromTL(result.data()));
		_listUpdated.fire({});
		done();
	}).fail([=](const MTP::Error &) {
		_pendingRegistration = nullptr;
	}).send();
}

void Passkeys::deletePasskey(
		const QString &id,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	_session->api().request(MTPaccount_DeletePasskey(
		MTP_string(id)
	)).done([=] {
		_lastRequestTime = 0;
		_listKnown = false;
		loadList();
		done();
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

rpl::producer<> Passkeys::requestList() {
	if (!_lastRequestTime
		|| (crl::now() - _lastRequestTime > kRequestTimeout)) {
		if (!_listRequestId) {
			loadList();
		}
		return _listUpdated.events();
	} else {
		return _listUpdated.events_starting_with(rpl::empty_value());
	}
}

const std::vector<PasskeyEntry> &Passkeys::list() const {
	return _passkeys;
}

bool Passkeys::listKnown() const {
	return _listKnown;
}

void Passkeys::loadList() {
	_lastRequestTime = crl::now();
	_listRequestId = _session->api().request(MTPaccount_GetPasskeys(
	)).done([=](const MTPaccount_Passkeys &result) {
		_listRequestId = 0;
		_listKnown = true;
		const auto &data = result.data();
		_passkeys.clear();
		_passkeys.reserve(data.vpasskeys().v.size());
		for (const auto &passkey : data.vpasskeys().v) {
			_passkeys.emplace_back(FromTL(passkey.data()));
		}
		_listUpdated.fire({});
	}).fail([=] {
		_listRequestId = 0;
	}).send();
}

bool Passkeys::canRegister() const {
	const auto max = _session->appConfig().passkeysAccountPasskeysMax();
	return Platform::WebAuthn::IsSupported() && _passkeys.size() < max;
}

bool Passkeys::possible() const {
	return _session->appConfig().settingsDisplayPasskeys();
}

void InitPasskeyLogin(
		MTP::Sender &api,
		Fn<void(const Data::Passkey::LoginData&)> done) {
	api.request(MTPauth_InitPasskeyLogin(
		MTP_int(ApiId),
		MTP_string(ApiHash)
	)).done([=](const MTPauth_PasskeyLoginOptions &result) {
		const auto &data = result.data();
		if (const auto p = Passkey::DeserializeLoginData(
				data.voptions().data().vdata().v)) {
			done(*p);
		}
	}).send();
}

void FinishPasskeyLogin(
		MTP::Sender &api,
		int initialDc,
		const Platform::WebAuthn::LoginResult &result,
		Fn<void(const MTPauth_Authorization&)> done,
		Fn<void(QString)> fail) {
	const auto userHandleStr = QString::fromUtf8(result.userHandle);
	const auto parts = userHandleStr.split(':');
	auto userDcOk = false;
	const auto userDc = (parts.size() == 2)
		? parts[0].toInt(&userDcOk)
		: 0;
	if (!userDcOk || userDc <= 0 || userDc >= MTP::kDcShift) {
		fail(u"PASSKEY_HANDLE_INVALID"_q);
		return;
	}
	const auto credentialIdBase64 = result.credentialId.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	const auto credential = MTP_inputPasskeyCredentialPublicKey(
		MTP_string(credentialIdBase64.toStdString()),
		MTP_string(credentialIdBase64.toStdString()),
		MTP_inputPasskeyResponseLogin(
			MTP_dataJSON(MTP_bytes(result.clientDataJSON)),
			MTP_bytes(result.authenticatorData),
			MTP_bytes(result.signature),
			MTP_string(userHandleStr.toStdString())
		)
	);
	auto fromAuthKeyId = uint64(0);
	for (const auto &key : api.instance().getKeysForWrite()) {
		if (key && key->dcId() == MTP::BareDcId(initialDc)) {
			fromAuthKeyId = key->keyId();
			break;
		}
	}
	api.instance().setMainDcId(userDc);
	api.request(MTPauth_FinishPasskeyLogin(
		MTP_flags(MTPauth_finishPasskeyLogin::Flag::f_from_dc_id),
		credential,
		MTP_int(initialDc),
		MTP_long(int64(fromAuthKeyId))
	)).toDC(
		userDc
	).done(done).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

} // namespace Data
