/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_controller.h"

#include "passport/passport_form_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "base/openssl_help.h"
#include "mainwindow.h"

namespace Passport {

FormRequest::FormRequest(
	UserId botId,
	const QStringList &scope,
	const QString &callbackUrl,
	const QString &publicKey)
: botId(botId)
, scope(scope)
, callbackUrl(callbackUrl)
, publicKey(publicKey) {
}


FormController::Field::Field(Type type) : type(type) {
}

FormController::FormController(
	not_null<Window::Controller*> controller,
	const FormRequest &request)
: _controller(controller)
, _request(request) {
	const auto url = QUrl(_request.callbackUrl);
	_origin = url.scheme() + "://" + url.host();
}

void FormController::show() {
	requestForm();
	requestPassword();
}

void FormController::submitPassword(const QString &password) {
	Expects(!_password.salt.isEmpty());

	if (_passwordCheckRequestId) {
		return;
	}
	const auto data = _password.salt + password.toUtf8() + _password.salt;
	const auto hash = hashSha256(data.constData(), data.size());
	_passwordCheckRequestId = request(MTPaccount_GetPasswordSettings(
		MTP_bytes(gsl::as_bytes(gsl::make_span(hash)))
	)).handleFloodErrors(
	).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		const auto &data = result.c_account_passwordSettings();
		_passwordEmail = qs(data.vemail);
		_secret = byteVectorFromMTP(data.vsecure_secret);
		_secretReady.fire({});
	}).fail([=](const RPCError &error) {
		_passwordCheckRequestId = 0;
		if (MTP::isFloodError(error)) {
			_passwordError.fire(lang(lng_flood_error));
		} else if (error.type() == qstr("PASSWORD_HASH_INVALID")) {
			_passwordError.fire(lang(lng_passport_password_wrong));
		} else {
			_passwordError.fire_copy(error.type());
		}
	}).send();
}

rpl::producer<QString> FormController::passwordError() const {
	return _passwordError.events();
}

QString FormController::passwordHint() const {
	return _password.hint;
}

rpl::producer<> FormController::secretReadyEvents() const {
	return _secretReady.events();
}

QString FormController::defaultEmail() const {
	return _passwordEmail;
}

QString FormController::defaultPhoneNumber() const {
	if (const auto self = App::self()) {
		return self->phone();
	}
	return QString();
}

void FormController::fillRows(
	base::lambda<void(
		QString title,
		QString description,
		bool ready)> callback) {
	for (const auto &field : _form.fields) {
		switch (field.type) {
		case Field::Type::Identity:
			callback(
				lang(lng_passport_identity_title),
				lang(lng_passport_identity_description),
				false);
			break;
		case Field::Type::Address:
			callback(
				lang(lng_passport_address_title),
				lang(lng_passport_address_description),
				false);
			break;
		case Field::Type::Phone:
			callback(
				lang(lng_passport_phone_title),
				App::self()->phone(),
				true);
			break;
		case Field::Type::Email:
			callback(
				lang(lng_passport_email_title),
				lang(lng_passport_email_description),
				false);
			break;
		}
	}
}

void FormController::requestForm() {
	auto scope = QVector<MTPstring>();
	scope.reserve(_request.scope.size());
	for (const auto &element : _request.scope) {
		scope.push_back(MTP_string(element));
	}
	auto normalizedKey = _request.publicKey;
	normalizedKey.replace("\r\n", "\n");
	const auto bytes = normalizedKey.toUtf8();
	_formRequestId = request(MTPaccount_GetAuthorizationForm(
		MTP_flags(MTPaccount_GetAuthorizationForm::Flag::f_origin
			| MTPaccount_GetAuthorizationForm::Flag::f_public_key),
		MTP_int(_request.botId),
		MTP_vector<MTPstring>(std::move(scope)),
		MTP_string(_origin),
		MTPstring(), // package_name
		MTPstring(), // bundle_id
		MTP_bytes(bytes)
	)).done([=](const MTPaccount_AuthorizationForm &result) {
		_formRequestId = 0;
		formDone(result);
	}).fail([=](const RPCError &error) {
		formFail(error);
	}).send();
}

auto FormController::convertValue(
		const MTPSecureValue &value) const -> Value {
	auto result = Value();
	switch (value.type()) {
	case mtpc_secureValueEmpty: {
		const auto &data = value.c_secureValueEmpty();
		result.name = qs(data.vname);
	} break;
	case mtpc_secureValueData: {
		const auto &data = value.c_secureValueData();
		result.name = qs(data.vname);
		result.data = data.vdata.v;
		result.dataHash = data.vhash.v;
		result.dataSecret = data.vsecret.v;
	} break;
	case mtpc_secureValueText: {
		const auto &data = value.c_secureValueText();
		result.name = qs(data.vname);
		result.text = qs(data.vtext);
		result.textHash = data.vhash.v;
	} break;
	case mtpc_secureValueFile: {
		const auto &data = value.c_secureValueFile();
		result.name = qs(data.vname);
		result.filesHash = data.vhash.v;
		result.filesSecret = data.vsecret.v;
		for (const auto &file : data.vfile.v) {
			switch (file.type()) {
			case mtpc_secureFileEmpty: {
				result.files.push_back(File());
			} break;
			case mtpc_secureFile: {
				const auto &fields = file.c_secureFile();
				auto normal = File();
				normal.id = fields.vid.v;
				normal.accessHash = fields.vaccess_hash.v;
				normal.size = fields.vsize.v;
				normal.dcId = fields.vdc_id.v;
				normal.fileHash = qba(fields.vfile_hash);
				result.files.push_back(std::move(normal));
			} break;
			}
		}
	} break;
	}
	return result;
}

void FormController::formDone(const MTPaccount_AuthorizationForm &result) {
	parseForm(result);
	if (!_passwordRequestId) {
		showForm();
	}
}

void FormController::parseForm(const MTPaccount_AuthorizationForm &result) {
	Expects(result.type() == mtpc_account_authorizationForm);

	const auto &data = result.c_account_authorizationForm();
	_form.requestWrite = data.is_request_write();
	if (_request.botId != data.vbot_id.v) {
		LOG(("API Error: Wrong account.authorizationForm.bot_id."));
		_request.botId = data.vbot_id.v;
	}
	App::feedUsers(data.vusers);
	for (const auto &field : data.vfields.v) {
		Assert(field.type() == mtpc_authField);

		using Type = Field::Type;

		const auto &data = field.c_authField();
		const auto type = [&] {
			switch (data.vtype.type()) {
			case mtpc_authFieldTypeIdentity: return Type::Identity;
			case mtpc_authFieldTypeAddress: return Type::Address;
			case mtpc_authFieldTypeEmail: return Type::Email;
			case mtpc_authFieldTypePhone: return Type::Phone;
			}
			Unexpected("Type in authField.type.");
		}();
		auto entry = Field(type);
		entry.data = convertValue(data.vdata);
		if (data.has_document()) {
			entry.document = convertValue(data.vdocument);
		}
		_form.fields.push_back(std::move(entry));
	}
	_bot = App::userLoaded(_request.botId);
}

void FormController::showForm() {
	if (!_bot) {
		Ui::show(Box<InformBox>("Could not get authorization bot."));
		return;
	}
	Ui::show(Box<FormBox>(this));
}

void FormController::formFail(const RPCError &error) {
	Ui::show(Box<InformBox>(lang(lng_passport_form_error)));
}

void FormController::requestPassword() {
	_passwordRequestId = request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_passwordRequestId = 0;
		passwordDone(result);
	}).fail([=](const RPCError &error) {
		formFail(error);
	}).send();
}

void FormController::passwordDone(const MTPaccount_Password &result) {
	switch (result.type()) {
	case mtpc_account_noPassword:
		parsePassword(result.c_account_noPassword());
		break;

	case mtpc_account_password:
		parsePassword(result.c_account_password());
		break;
	}
	if (!_formRequestId) {
		showForm();
	}
}

void FormController::passwordFail(const RPCError &error) {
	Ui::show(Box<InformBox>("Could not get authorization form."));
}

void FormController::parsePassword(const MTPDaccount_noPassword &result) {
	_password.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	_password.newSalt = result.vnew_salt.v;
	openssl::AddRandomSeed(
		gsl::as_bytes(gsl::make_span(result.vsecret_random.v)));
}

void FormController::parsePassword(const MTPDaccount_password &result) {
	_password.hint = qs(result.vhint);
	_password.hasRecovery = mtpIsTrue(result.vhas_recovery);
	_password.salt = result.vcurrent_salt.v;
	_password.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	_password.newSalt = result.vnew_salt.v;
	openssl::AddRandomSeed(
		gsl::as_bytes(gsl::make_span(result.vsecret_random.v)));
}

} // namespace Passport
