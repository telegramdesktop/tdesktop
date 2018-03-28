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
#include "auth_session.h"
#include "storage/localimageloader.h"
#include "storage/file_upload.h"
#include "storage/file_download.h"

namespace Passport {
namespace {

QImage ReadImage(bytes::const_span buffer) {
	return App::readImage(QByteArray::fromRawData(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size()));
}

} // namespace

FormRequest::FormRequest(
	UserId botId,
	const QString &scope,
	const QString &callbackUrl,
	const QString &publicKey)
: botId(botId)
, scope(scope)
, callbackUrl(callbackUrl)
, publicKey(publicKey) {
}

FormController::UploadedScan::~UploadedScan() {
	if (fullId) {
		Auth().uploader().cancel(fullId);
	}
}

FormController::EditFile::EditFile(
	const File &fields,
	std::unique_ptr<UploadedScan> &&uploaded)
: fields(std::move(fields))
, uploaded(std::move(uploaded)) {
}

FormController::Field::Field(Type type) : type(type) {
}

template <typename FileHashes>
bytes::vector FormController::computeFilesHash(
		FileHashes fileHashes,
		bytes::const_span valueSecret) {
	auto vec = bytes::concatenate(fileHashes);
	auto hashesVector = std::vector<bytes::const_span>();
	for (const auto &hash : fileHashes) {
		hashesVector.push_back(bytes::make_span(hash));
	}
	return PrepareFilesHash(hashesVector, valueSecret);
}

FormController::FormController(
	not_null<Window::Controller*> controller,
	const FormRequest &request)
: _controller(controller)
, _request(request) {
}

void FormController::show() {
	requestForm();
	requestPassword();
}

bytes::vector FormController::passwordHashForAuth(
		bytes::const_span password) const {
	return openssl::Sha256(bytes::concatenate(
		_password.salt,
		password,
		_password.salt));
}

void FormController::submitPassword(const QString &password) {
	Expects(!_password.salt.empty());

	if (_passwordCheckRequestId) {
		return;
	} else if (password.isEmpty()) {
		_passwordError.fire(QString());
	}
	const auto passwordBytes = password.toUtf8();
	_passwordCheckRequestId = request(MTPaccount_GetPasswordSettings(
		MTP_bytes(passwordHashForAuth(bytes::make_span(passwordBytes)))
	)).handleFloodErrors(
	).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		const auto &data = result.c_account_passwordSettings();
		_password.confirmedEmail = qs(data.vemail);
		validateSecureSecret(
			bytes::make_span(data.vsecure_salt.v),
			bytes::make_span(data.vsecure_secret.v),
			bytes::make_span(passwordBytes));
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

void FormController::validateSecureSecret(
		bytes::const_span salt,
		bytes::const_span encryptedSecret,
		bytes::const_span password) {
	if (!salt.empty() && !encryptedSecret.empty()) {
		_secret = DecryptSecureSecret(salt, encryptedSecret, password);
		if (_secret.empty()) {
			LOG(("API Error: Failed to decrypt secure secret. "
				"Forgetting all files and data :("));
			for (auto &field : _form.fields) {
				if (!field.encryptedSecret.empty()) {
					resetField(field);
				}
			}
		} else {
			decryptFields();
		}
	}
	if (_secret.empty()) {
		generateSecret(password);
	}
	_secretReady.fire({});
}

void FormController::decryptFields() {
	Expects(!_secret.empty());

	for (auto &field : _form.fields) {
		if (field.encryptedSecret.empty()) {
			continue;
		}
		decryptField(field);
	}
}

void FormController::decryptField(Field &field) {
	Expects(!_secret.empty());
	Expects(!field.encryptedSecret.empty());

	if (!validateFieldSecret(field)) {
		resetField(field);
		return;
	}
	const auto dataHash = bytes::make_span(field.dataHash);
	field.parsedData = DeserializeData(DecryptData(
		bytes::make_span(field.originalData),
		field.dataHash,
		field.secret));
}

bool FormController::validateFieldSecret(Field &field) {
	field.secret = DecryptValueSecret(
		field.encryptedSecret,
		_secret,
		field.hash);
	if (field.secret.empty()) {
		LOG(("API Error: Could not decrypt value secret. "
			"Forgetting files and data :("));
		return false;
	}
	const auto fileHashes = ranges::view::all(
		field.files
	) | ranges::view::transform([](File &file) {
		return bytes::make_span(file.fileHash);
	});
	const auto countedHash = openssl::Sha256(bytes::concatenate(
		field.dataHash,
		bytes::concatenate(fileHashes),
		field.secret));
	if (field.hash != countedHash) {
		LOG(("API Error: Wrong hash after decrypting value secret. "
			"Forgetting files and data :("));
		return false;
	}
	return true;
}

void FormController::resetField(Field &field) {
	field = Field(field.type);
}

rpl::producer<QString> FormController::passwordError() const {
	return _passwordError.events();
}

QString FormController::passwordHint() const {
	return _password.hint;
}

void FormController::uploadScan(int fieldIndex, QByteArray &&content) {
	Expects(_editBox != nullptr);
	Expects(fieldIndex >= 0 && fieldIndex < _form.fields.size());

	auto &field = _form.fields[fieldIndex];
	if (field.secret.empty()) {
		field.secret = GenerateSecretBytes();
	}
	auto fileIndex = int(field.filesInEdit.size());
	field.filesInEdit.emplace_back(
		File(),
		nullptr);
	const auto fileId = rand_value<uint64>();
	auto &file = field.filesInEdit.back();
	file.fields.size = content.size();
	file.fields.id = fileId;
	file.fields.dcId = MTP::maindc();
	file.fields.image = ReadImage(bytes::make_span(content));
	file.fields.downloadOffset = file.fields.size;

	_scanUpdated.fire(collectScanInfo(file));

	encryptScan(fieldIndex, fileIndex, std::move(content));
}

void FormController::encryptScan(
		int fieldIndex,
		int fileIndex,
		QByteArray &&content) {
	Expects(_editBox != nullptr);
	Expects(fieldIndex >= 0 && fieldIndex < _form.fields.size());
	Expects(fileIndex >= 0
		&& fileIndex < _form.fields[fieldIndex].filesInEdit.size());

	auto &field = _form.fields[fieldIndex];
	const auto weak = _editBox;
	crl::async([
		=,
		fileId = field.filesInEdit[fileIndex].fields.id,
		bytes = std::move(content),
		valueSecret = field.secret
	] {
		auto data = EncryptData(
			bytes::make_span(bytes),
			valueSecret);
		auto result = UploadedScan();
		result.fileId = fileId;
		result.hash = std::move(data.hash);
		result.bytes = std::move(data.bytes);
		result.md5checksum.resize(32);
		hashMd5Hex(
			result.bytes.data(),
			result.bytes.size(),
			result.md5checksum.data());
		crl::on_main([=, encrypted = std::move(result)]() mutable {
			if (weak) {
				uploadEncryptedScan(
					fieldIndex,
					fileIndex,
					std::move(encrypted));
			}
		});
	});
}

void FormController::deleteScan(
		int fieldIndex,
		int fileIndex) {
	Expects(fieldIndex >= 0 && fieldIndex < _form.fields.size());
	Expects(fileIndex >= 0
		&& fileIndex < _form.fields[fieldIndex].filesInEdit.size());

	auto &file = _form.fields[fieldIndex].filesInEdit[fileIndex];
	file.deleted = !file.deleted;
	_scanUpdated.fire(collectScanInfo(file));
}

void FormController::subscribeToUploader() {
	if (_uploaderSubscriptions) {
		return;
	}

	using namespace Storage;
	Auth().uploader().secureReady(
	) | rpl::start_with_next([=](const UploadSecureDone &data) {
		scanUploadDone(data);
	}, _uploaderSubscriptions);
	Auth().uploader().secureProgress(
	) | rpl::start_with_next([=](const UploadSecureProgress &data) {
		scanUploadProgress(data);
	}, _uploaderSubscriptions);
	Auth().uploader().secureFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		scanUploadFail(fullId);
	}, _uploaderSubscriptions);
}

void FormController::uploadEncryptedScan(
		int fieldIndex,
		int fileIndex,
		UploadedScan &&data) {
	Expects(_editBox != nullptr);
	Expects(fieldIndex >= 0 && fieldIndex < _form.fields.size());
	Expects(fileIndex >= 0
		&& fileIndex < _form.fields[fieldIndex].filesInEdit.size());

	subscribeToUploader();

	auto &file = _form.fields[fieldIndex].filesInEdit[fileIndex];
	file.uploaded = std::make_unique<UploadedScan>(std::move(data));

	auto uploaded = std::make_shared<FileLoadResult>(
		TaskId(),
		file.uploaded->fileId,
		FileLoadTo(PeerId(0), false, MsgId(0)),
		TextWithTags(),
		std::shared_ptr<SendingAlbum>(nullptr));
	uploaded->type = SendMediaType::Secure;
	uploaded->content = QByteArray::fromRawData(
		reinterpret_cast<char*>(file.uploaded->bytes.data()),
		file.uploaded->bytes.size());
	uploaded->setFileData(uploaded->content);
	uploaded->filemd5 = file.uploaded->md5checksum;

	file.uploaded->fullId = FullMsgId(0, clientMsgId());
	Auth().uploader().upload(file.uploaded->fullId, std::move(uploaded));
}

void FormController::scanUploadDone(const Storage::UploadSecureDone &data) {
	if (const auto file = findEditFile(data.fullId)) {
		Assert(file->uploaded != nullptr);
		Assert(file->uploaded->fileId == data.fileId);

		file->uploaded->partsCount = data.partsCount;
		file->fields.fileHash = std::move(file->uploaded->hash);
		file->uploaded->fullId = FullMsgId();

		_scanUpdated.fire(collectScanInfo(*file));
	}
}

void FormController::scanUploadProgress(
		const Storage::UploadSecureProgress &data) {
	if (const auto file = findEditFile(data.fullId)) {
		Assert(file->uploaded != nullptr);

		file->uploaded->offset = data.offset;

		_scanUpdated.fire(collectScanInfo(*file));
	}
}

void FormController::scanUploadFail(const FullMsgId &fullId) {
	if (const auto file = findEditFile(fullId)) {
		Assert(file->uploaded != nullptr);

		file->uploaded->offset = -1;

		_scanUpdated.fire(collectScanInfo(*file));
	}
}

rpl::producer<> FormController::secretReadyEvents() const {
	return _secretReady.events();
}

QString FormController::defaultEmail() const {
	return _password.confirmedEmail;
}

QString FormController::defaultPhoneNumber() const {
	if (const auto self = App::self()) {
		return self->phone();
	}
	return QString();
}

rpl::producer<ScanInfo> FormController::scanUpdated() const {
	return _scanUpdated.events();
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
		auto &field = _form.fields[index];
		switch (field.type) {
		case Field::Type::Identity:
			loadFiles(field.files);
			field.filesInEdit = ranges::view::all(
				field.files
			) | ranges::view::transform([](const File &file) {
				return EditFile(file, nullptr);
			}) | ranges::to_vector;
			return Box<IdentityBox>(
				this,
				index,
				fieldDataIdentity(field),
				fieldFilesIdentity(field));
		}
		return { nullptr };
	}();
	if (box) {
		_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
	}
}

void FormController::loadFiles(std::vector<File> &files) {
	for (auto &file : files) {
		if (!file.image.isNull()) {
			file.downloadOffset = file.size;
			continue;
		}

		const auto key = FileKey{ file.id, file.dcId };
		const auto i = _fileLoaders.find(key);
		if (i == _fileLoaders.end()) {
			file.downloadOffset = 0;
			const auto [i, ok] = _fileLoaders.emplace(
				key,
				std::make_unique<mtpFileLoader>(
					file.dcId,
					file.id,
					file.accessHash,
					0,
					SecureFileLocation,
					QString(),
					file.size,
					LoadToCacheAsWell,
					LoadFromCloudOrLocal,
					false));
			const auto loader = i->second.get();
			loader->connect(loader, &mtpFileLoader::progress, [=] {
				if (loader->finished()) {
					fileLoadDone(key, loader->bytes());
				} else {
					fileLoadProgress(key, loader->currentOffset());
				}
			});
			loader->connect(loader, &mtpFileLoader::failed, [=] {
				fileLoadFail(key);
			});
			loader->start();
		}
	}
}

void FormController::fileLoadDone(FileKey key, const QByteArray &bytes) {
	if (const auto [field, file] = findFile(key); file != nullptr) {
		const auto decrypted = DecryptData(
			bytes::make_span(bytes),
			file->fileHash,
			field->secret);
		file->downloadOffset = file->size;
		file->image = App::readImage(QByteArray::fromRawData(
			reinterpret_cast<const char*>(decrypted.data()),
			decrypted.size()));
		if (const auto fileInEdit = findEditFile(key)) {
			fileInEdit->fields.image = file->image;
			fileInEdit->fields.downloadOffset = file->downloadOffset;
			_scanUpdated.fire(collectScanInfo(*fileInEdit));
		}
	}
}

void FormController::fileLoadProgress(FileKey key, int offset) {
	if (const auto [field, file] = findFile(key); file != nullptr) {
		file->downloadOffset = offset;
		if (const auto fileInEdit = findEditFile(key)) {
			fileInEdit->fields.downloadOffset = file->downloadOffset;
			_scanUpdated.fire(collectScanInfo(*fileInEdit));
		}
	}
}

void FormController::fileLoadFail(FileKey key) {
	if (const auto [field, file] = findFile(key); file != nullptr) {
		file->downloadOffset = -1;
		if (const auto fileInEdit = findEditFile(key)) {
			fileInEdit->fields.downloadOffset = file->downloadOffset;
			_scanUpdated.fire(collectScanInfo(*fileInEdit));
		}
	}
}

ScanInfo FormController::collectScanInfo(const EditFile &file) const {
	const auto status = [&] {
		if (file.deleted) {
			return QString("deleted");
		} else if (file.fields.accessHash) {
			if (file.fields.downloadOffset < 0) {
				return QString("download failed");
			} else if (file.fields.downloadOffset < file.fields.size) {
				return QString("downloading %1 / %2"
				).arg(file.fields.downloadOffset
				).arg(file.fields.size);
			} else {
				return QString("download ready");
			}
		} else if (file.uploaded) {
			if (file.uploaded->offset < 0) {
				return QString("upload failed");
			} else if (file.uploaded->fullId) {
				return QString("uploading %1 / %2"
				).arg(file.uploaded->offset
				).arg(file.uploaded->bytes.size());
			} else {
				return QString("upload ready");
			}
		} else {
			return QString("preparing");
		}
	}();
	return {
		FileKey{ file.fields.id, file.fields.dcId },
		status,
		file.fields.image };
}

IdentityData FormController::fieldDataIdentity(const Field &field) const {
	const auto &map = field.parsedData;
	auto result = IdentityData();
	if (const auto i = map.find(qsl("first_name")); i != map.cend()) {
		result.name = i->second;
	}
	if (const auto i = map.find(qsl("last_name")); i != map.cend()) {
		result.surname = i->second;
	}
	return result;
}

std::vector<ScanInfo> FormController::fieldFilesIdentity(
		const Field &field) const {
	auto result = std::vector<ScanInfo>();
	for (const auto &file : field.filesInEdit) {
		result.push_back(collectScanInfo(file));
	}
	return result;
}

void FormController::saveFieldIdentity(
		int index,
		const IdentityData &data) {
	Expects(_editBox != nullptr);
	Expects(index >= 0 && index < _form.fields.size());
	Expects(_form.fields[index].type == Field::Type::Identity);

	_form.fields[index].parsedData[qsl("first_name")] = data.name;
	_form.fields[index].parsedData[qsl("last_name")] = data.surname;

	saveIdentity(index);
}

void FormController::saveIdentity(int index) {
	Expects(index >= 0 && index < _form.fields.size());
	Expects(_form.fields[index].type == Field::Type::Identity);

	if (_secret.empty()) {
		_secretCallbacks.push_back([=] {
			saveIdentity(index);
		});
		return;
	}

	_editBox->closeBox();

	auto &field = _form.fields[index];

	auto inputFiles = QVector<MTPInputSecureFile>();
	inputFiles.reserve(field.filesInEdit.size());
	for (const auto &file : field.filesInEdit) {
		if (file.deleted) {
			continue;
		} else if (const auto uploaded = file.uploaded.get()) {
			inputFiles.push_back(MTP_inputSecureFileUploaded(
				MTP_long(file.fields.id),
				MTP_int(uploaded->partsCount),
				MTP_bytes(uploaded->md5checksum),
				MTP_bytes(file.fields.fileHash)));
		} else {
			inputFiles.push_back(MTP_inputSecureFile(
				MTP_long(file.fields.id),
				MTP_long(file.fields.accessHash)));
		}
	}
	if (field.secret.empty()) {
		field.secret = GenerateSecretBytes();
	}
	const auto encryptedData = EncryptData(
		SerializeData(field.parsedData),
		field.secret);
	const auto fileHashes = ranges::view::all(
		field.filesInEdit
	) | ranges::view::filter([](const EditFile &file) {
		return !file.deleted;
	}) | ranges::view::transform([](const EditFile &file) {
		return bytes::make_span(file.fields.fileHash);
	});
	const auto valueHash = openssl::Sha256(bytes::concatenate(
		encryptedData.hash,
		bytes::concatenate(fileHashes),
		field.secret));

	field.encryptedSecret = EncryptValueSecret(
		field.secret,
		_secret,
		valueHash);
	request(MTPaccount_SaveSecureValue(
		MTP_inputSecureValueIdentity(
			MTP_secureData(
				MTP_bytes(encryptedData.bytes),
				MTP_bytes(encryptedData.hash)),
			MTP_vector<MTPInputSecureFile>(inputFiles),
			MTP_bytes(field.encryptedSecret),
			MTP_bytes(valueHash)),
		MTP_long(CountSecureSecretHash(_secret))
	)).done([=](const MTPSecureValueSaved &result) {
		Expects(result.type() == mtpc_secureValueSaved);

		const auto &data = result.c_secureValueSaved();
		_form.fields[index].files = parseFiles(
			data.vfiles.v,
			base::take(_form.fields[index].filesInEdit));

		Ui::show(Box<InformBox>("Saved"), LayerOption::KeepOther);
	}).fail([=](const RPCError &error) {
		Ui::show(Box<InformBox>("Error saving value."));
	}).send();
}

void FormController::generateSecret(bytes::const_span password) {
	if (_saveSecretRequestId) {
		return;
	}
	auto secret = GenerateSecretBytes();

	auto randomSaltPart = bytes::vector(8);
	bytes::set_random(randomSaltPart);
	auto newSecureSaltFull = bytes::concatenate(
		_password.newSecureSalt,
		randomSaltPart);

	const auto hashForSecret = openssl::Sha512(bytes::concatenate(
		newSecureSaltFull,
		password,
		newSecureSaltFull));
	const auto hashForAuth = openssl::Sha256(bytes::concatenate(
		_password.salt,
		password,
		_password.salt));

	auto secureSecretHash = CountSecureSecretHash(secret);
	auto encryptedSecret = EncryptSecretBytes(
		secret,
		hashForSecret);
	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_saveSecretRequestId = request(MTPaccount_UpdatePasswordSettings(
		MTP_bytes(hashForAuth),
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_secret),
			MTPbytes(), // new_salt
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_bytes(newSecureSaltFull),
			MTP_bytes(encryptedSecret),
			MTP_long(secureSecretHash))
	)).done([=](const MTPBool &result) {
		_saveSecretRequestId = 0;
		_secret = secret;
		//_password.salt = newPasswordSaltFull;
		for (const auto &callback : base::take(_secretCallbacks)) {
			callback();
		}
	}).fail([=](const RPCError &error) {
		// #TODO wrong password hash error?
		Ui::show(Box<InformBox>("Saving encrypted value failed."));
		_saveSecretRequestId = 0;
	}).send();
}

void FormController::requestForm() {
	auto normalizedKey = _request.publicKey;
	normalizedKey.replace("\r\n", "\n");
	_formRequestId = request(MTPaccount_GetAuthorizationForm(
		MTP_int(_request.botId),
		MTP_string(_request.scope),
		MTP_bytes(normalizedKey.toUtf8())
	)).done([=](const MTPaccount_AuthorizationForm &result) {
		_formRequestId = 0;
		formDone(result);
	}).fail([=](const RPCError &error) {
		formFail(error);
	}).send();
}

template <typename DataType>
auto FormController::parseEncryptedField(
		Field::Type type,
		const DataType &data) const -> Field {
	Expects(data.vdata.type() == mtpc_secureData);

	auto result = Field(type);
	if (data.has_verified()) {
		result.verification = parseVerified(data.vverified);
	}
	result.encryptedSecret = bytes::make_vector(data.vsecret.v);
	result.hash = bytes::make_vector(data.vhash.v);
	const auto &fields = data.vdata.c_secureData();
	result.originalData = fields.vdata.v;
	result.dataHash = bytes::make_vector(fields.vdata_hash.v);
	result.files = parseFiles(data.vfiles.v);
	return result;
}

auto FormController::parseFiles(
	const QVector<MTPSecureFile> &data,
	const std::vector<EditFile> &editData) const
-> std::vector<File> {
	auto result = std::vector<File>();
	result.reserve(data.size());

	auto index = 0;
	for (const auto &file : data) {
		switch (file.type()) {
		case mtpc_secureFileEmpty: {

		} break;
		case mtpc_secureFile: {
			const auto &fields = file.c_secureFile();
			auto normal = File();
			normal.id = fields.vid.v;
			normal.accessHash = fields.vaccess_hash.v;
			normal.size = fields.vsize.v;
			normal.dcId = fields.vdc_id.v;
			normal.fileHash = bytes::make_vector(fields.vfile_hash.v);
			const auto i = ranges::find(
				editData,
				normal.fileHash,
				[](const EditFile &file) { return file.fields.fileHash; });
			if (i != editData.end()) {
				normal.image = i->fields.image;
				normal.downloadOffset = i->fields.downloadOffset;
			}
			result.push_back(std::move(normal));
		} break;
		}
		++index;
	}

	return result;
}

template <typename DataType>
auto FormController::parsePlainTextField(
		Field::Type type,
		const QByteArray &value,
		const DataType &data) const -> Field {
	auto result = Field(type);
	const auto check = bytes::compare(
		bytes::make_span(data.vhash.v),
		openssl::Sha256(bytes::make_span(value)));
	if (check != 0) {
		LOG(("API Error: Bad hash for plain text value. "
			"Value '%1', hash '%2'"
			).arg(QString::fromUtf8(value)
			).arg(Logs::mb(data.vhash.v.data(), data.vhash.v.size()).str()
			));
		return result;
	}
	result.parsedData[QString("value")] = QString::fromUtf8(value);
	if (data.has_verified()) {
		result.verification = parseVerified(data.vverified);
	}
	result.hash = bytes::make_vector(data.vhash.v);
	return result;
}

auto FormController::parseValue(
		const MTPSecureValue &value) const -> Field {
	switch (value.type()) {
	case mtpc_secureValueIdentity: {
		return parseEncryptedField(
			Field::Type::Identity,
			value.c_secureValueIdentity());
	} break;
	case mtpc_secureValueAddress: {
		return parseEncryptedField(
			Field::Type::Address,
			value.c_secureValueAddress());
	} break;
	case mtpc_secureValuePhone: {
		const auto &data = value.c_secureValuePhone();
		return parsePlainTextField(
			Field::Type::Phone,
			data.vphone.v,
			data);
	} break;
	case mtpc_secureValueEmail: {
		const auto &data = value.c_secureValueEmail();
		return parsePlainTextField(
			Field::Type::Phone,
			data.vemail.v,
			data);
	} break;
	}
	Unexpected("secureValue type.");
}

auto FormController::parseVerified(const MTPSecureValueVerified &data) const
-> Verification {
	Expects(data.type() == mtpc_secureValueVerified);

	const auto &fields = data.c_secureValueVerified();
	return Verification{ fields.vdate.v, qs(fields.vprovider) };
}

auto FormController::findEditFile(const FullMsgId &fullId) -> EditFile* {
	for (auto &field : _form.fields) {
		for (auto &file : field.filesInEdit) {
			if (file.uploaded && file.uploaded->fullId == fullId) {
				return &file;
			}
		}
	}
	return nullptr;
}

auto FormController::findEditFile(const FileKey &key) -> EditFile* {
	for (auto &field : _form.fields) {
		for (auto &file : field.filesInEdit) {
			if (file.fields.dcId == key.dcId && file.fields.id == key.id) {
				return &file;
			}
		}
	}
	return nullptr;
}

auto FormController::findFile(const FileKey &key)
-> std::pair<Field*, File*> {
	for (auto &field : _form.fields) {
		for (auto &file : field.files) {
			if (file.dcId == key.dcId && file.id == key.id) {
				return { &field, &file };
			}
		}
	}
	return { nullptr, nullptr };
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

	auto values = std::vector<Field>();
	for (const auto &value : data.vvalues.v) {
		values.push_back(parseValue(value));
	}
	const auto findValue = [&](Field::Type type) -> Field* {
		for (auto &value : values) {
			if (value.type == type) {
				return &value;
			}
		}
		return nullptr;
	};

	_form.requestWrite = false;
	App::feedUsers(data.vusers);
	for (const auto &required : data.vrequired_types.v) {
		using Type = Field::Type;

		const auto type = [&] {
			switch (required.type()) {
			case mtpc_secureValueTypeIdentity: return Type::Identity;
			case mtpc_secureValueTypeAddress: return Type::Address;
			case mtpc_secureValueTypeEmail: return Type::Email;
			case mtpc_secureValueTypePhone: return Type::Phone;
			}
			Unexpected("Type in secureValueType type.");
		}();

		if (auto field = findValue(type)) {
			_form.fields.push_back(std::move(*field));
		} else {
			_form.fields.push_back(Field(type));
		}
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
	_password.newSalt = bytes::make_vector(result.vnew_salt.v);
	_password.newSecureSalt = bytes::make_vector(result.vnew_secure_salt.v);
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random.v));
}

void FormController::parsePassword(const MTPDaccount_password &result) {
	_password.hint = qs(result.vhint);
	_password.hasRecovery = mtpIsTrue(result.vhas_recovery);
	_password.salt = bytes::make_vector(result.vcurrent_salt.v);
	_password.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	_password.newSalt = bytes::make_vector(result.vnew_salt.v);
	_password.newSecureSalt = bytes::make_vector(result.vnew_secure_salt.v);
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random.v));
}

FormController::~FormController() = default;

} // namespace Passport
