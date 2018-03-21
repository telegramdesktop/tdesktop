/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_controller.h"

#include "passport/passport_form_box.h"
#include "passport/passport_edit_identity_box.h"
#include "passport/passport_encryption.h"
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
	} else if (password.isEmpty()) {
		_passwordError.fire(QString());
	}
	const auto passwordBytes = password.toUtf8();
	const auto data = _password.salt + passwordBytes + _password.salt;
	const auto hash = openssl::Sha256(gsl::as_bytes(gsl::make_span(data)));
	_passwordHashForAuth = { hash.begin(), hash.end() };
	_passwordCheckRequestId = request(MTPaccount_GetPasswordSettings(
		MTP_bytes(_passwordHashForAuth)
	)).handleFloodErrors(
	).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		const auto &data = result.c_account_passwordSettings();
		_passwordEmail = qs(data.vemail);
		const auto hash = openssl::Sha512(gsl::as_bytes(gsl::make_span(passwordBytes)));
		_passwordHashForSecret = { hash.begin(), hash.end() };
		_secret = DecryptSecretBytes(
			bytesFromMTP(data.vsecure_secret),
			_passwordHashForSecret);
		for (auto &field : _form.fields) {
			field.data.values = fillData(field.data);
		}
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

void FormController::editField(int index) {
	Expects(index >= 0 && index < _form.fields.size());

	auto box = [&]() -> object_ptr<BoxContent> {
		const auto &field = _form.fields[index];
		switch (field.type) {
		case Field::Type::Identity:
			return Box<IdentityBox>(this, index, fieldDataIdentity(field));
		}
		return { nullptr };
	}();
	if (box) {
		_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
	}
}

IdentityData FormController::fieldDataIdentity(const Field &field) const {
	const auto &map = field.data.values;
	auto result = IdentityData();
	if (const auto i = map.find(qsl("first_name")); i != map.cend()) {
		result.name = i->second;
	}
	if (const auto i = map.find(qsl("last_name")); i != map.cend()) {
		result.surname = i->second;
	}
	return result;
}

void FormController::saveFieldIdentity(
		int index,
		const IdentityData &data) {
	Expects(_editBox != nullptr);
	Expects(index >= 0 && index < _form.fields.size());
	Expects(_form.fields[index].type == Field::Type::Identity);

	_form.fields[index].data.values[qsl("first_name")] = data.name;
	_form.fields[index].data.values[qsl("last_name")] = data.surname;

	saveData(index);

	_editBox->closeBox();
}

std::map<QString, QString> FormController::fillData(
		const Value &from) const {
	if (from.data.isEmpty()) {
		return {};
	}
	const auto valueHash = gsl::as_bytes(gsl::make_span(from.dataHash));
	const auto valueSecret = DecryptValueSecret(
		gsl::as_bytes(gsl::make_span(from.dataSecret)),
		_secret,
		valueHash);
	return DecryptData(
		gsl::as_bytes(gsl::make_span(from.data)),
		valueHash,
		valueSecret);
}

void FormController::saveData(int index) {
	Expects(index >= 0 && index < _form.fields.size());

	if (_secret.empty()) {
		generateSecret([=] {
			saveData(index);
		});
		return;
	}
	const auto &data = _form.fields[index].data;
	const auto valueSecret = GenerateSecretBytes();
	const auto encrypted = EncryptData(valueSecret, data.values);

	// #TODO file_hash + file_hash + ...
	// PrepareValueHash(encrypted.hash, valueSecret);
	const auto valueHash = encrypted.hash;
	auto valueHashString = QString();
	valueHashString.reserve(valueHash.size() * 2);
	const auto hex = [](uchar value) -> QChar {
		return (value >= 10) ? ('a' + (value - 10)) : ('0' + value);
	};
	for (const auto byte : valueHash) {
		const auto value = uchar(byte);
		const auto high = uchar(value / 16);
		const auto low = uchar(value % 16);
		valueHashString.append(hex(high)).append(hex(low));
	}

	const auto encryptedValueSecret = EncryptValueSecret(
		valueSecret,
		_secret,
		valueHash);
	request(MTPaccount_SaveSecureValue(MTP_inputSecureValueData(
		MTP_string(data.name),
		MTP_bytes(encrypted.bytes),
		MTP_string(valueHashString),
		MTP_bytes(encryptedValueSecret)
	))).done([=](const MTPaccount_SecureValueResult &result) {
		if (result.type() == mtpc_account_secureValueResultSaved) {
			Ui::show(Box<InformBox>("Saved"), LayerOption::KeepOther);
		} else if (result.type() == mtpc_account_secureValueVerificationNeeded) {
			Ui::show(Box<InformBox>("Verification needed :("), LayerOption::KeepOther);
		}
	}).fail([=](const RPCError &error) {
		// #TODO
	}).send();
}

void FormController::generateSecret(base::lambda<void()> callback) {
	if (_saveSecretRequestId) {
		return;
	}
	auto secret = GenerateSecretBytes();
	auto encryptedSecret = EncryptSecretBytes(
		secret,
		_passwordHashForSecret);
	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_saveSecretRequestId = request(MTPaccount_UpdatePasswordSettings(
		MTP_bytes(_passwordHashForAuth),
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_secret),
			MTPbytes(), // new_salt
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_bytes(encryptedSecret))
	)).done([=](const MTPBool &result) {
		_saveSecretRequestId = 0;
		_secret = secret;
		callback();
	}).fail([=](const RPCError &error) {
		// #TODO wrong password hash error?
		Ui::show(Box<InformBox>("Saving encrypted value failed."));
		_saveSecretRequestId = 0;
	}).send();
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
		const auto hashString = qs(data.vhash);
		for (auto i = 0, count = hashString.size(); i + 1 < count; i += 2) {
			auto digit = [&](QChar ch) -> int {
				const auto code = ch.unicode();
				if (code >= 'a' && code <= 'f') {
					return (code - 'a') + 10;
				} else if (code >= 'A' && code <= 'F') {
					return (code - 'A') + 10;
				} else if (code >= '0' && code <= '9') {
					return (code - '0');
				}
				return -1;
			};
			const auto ch1 = digit(hashString[i]);
			const auto ch2 = digit(hashString[i + 1]);
			if (ch1 >= 0 && ch2 >= 0) {
				const auto byte = ch1 * 16 + ch2;
				result.dataHash.push_back(byte);
			}
		}
		if (result.dataHash.size() != 32) {
			result.dataHash.clear();
		}
//		result.dataHash = data.vhash.v;
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
