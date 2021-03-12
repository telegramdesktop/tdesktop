/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "boxes/confirm_phone_box.h"
#include "base/weak_ptr.h"
#include "core/core_cloud_password.h"

class mtpFileLoader;

namespace Storage {
struct UploadSecureDone;
struct UploadSecureProgress;
} // namespace Storage

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

namespace Passport {

struct Config {
	int32 hash = 0;
	std::map<QString, QString> languagesByCountryCode;
};
Config &ConfigInstance();
Config ParseConfig(const MTPhelp_PassportConfig &data);

struct SavedCredentials {
	bytes::vector hashForAuth;
	bytes::vector hashForSecret;
	uint64 secretId = 0;
};

QString NonceNameByScope(const QString &scope);

class ViewController;

struct FormRequest {
	FormRequest(
		UserId botId,
		const QString &scope,
		const QString &callbackUrl,
		const QString &publicKey,
		const QString &nonce,
		const QString &errors);

	UserId botId;
	QString scope;
	QString callbackUrl;
	QString publicKey;
	QString nonce;
	QString errors;

};

struct UploadScanData {
	FullMsgId fullId;
	uint64 fileId = 0;
	int partsCount = 0;
	QByteArray md5checksum;
	bytes::vector hash;
	bytes::vector bytes;

	int offset = 0;
};

class UploadScanDataPointer {
public:
	UploadScanDataPointer(
		not_null<Main::Session*> session,
		std::unique_ptr<UploadScanData> &&value);
	UploadScanDataPointer(UploadScanDataPointer &&other);
	UploadScanDataPointer &operator=(UploadScanDataPointer &&other);
	~UploadScanDataPointer();

	UploadScanData *get() const;
	operator UploadScanData*() const;
	explicit operator bool() const;
	UploadScanData *operator->() const;

private:
	not_null<Main::Session*> _session;
	std::unique_ptr<UploadScanData> _value;

};

struct Value;

enum class FileType {
	Scan,
	Translation,
	FrontSide,
	ReverseSide,
	Selfie,
};

struct File {
	uint64 id = 0;
	uint64 accessHash = 0;
	int32 size = 0;
	int32 dcId = 0;
	TimeId date = 0;
	bytes::vector hash;
	bytes::vector secret;
	bytes::vector encryptedSecret;

	int downloadOffset = 0;
	QImage image;
	QString error;
};

struct EditFile {
	EditFile(
		not_null<Main::Session*> session,
		not_null<const Value*> value,
		FileType type,
		const File &fields,
		std::unique_ptr<UploadScanData> &&uploadData);

	not_null<const Value*> value;
	FileType type;
	File fields;
	UploadScanDataPointer uploadData;
	std::shared_ptr<bool> guard;
	bool deleted = false;
};

struct ValueField {
	QString text;
	QString error;
};

struct ValueMap {
	std::map<QString, ValueField> fields;
};

struct ValueData {
	QByteArray original;
	bytes::vector secret;
	ValueMap parsed;
	bytes::vector hash;
	bytes::vector encryptedSecret;
	ValueMap parsedInEdit;
	bytes::vector hashInEdit;
	bytes::vector encryptedSecretInEdit;
};

struct Verification {
	mtpRequestId requestId = 0;
	QString phoneCodeHash;
	int codeLength = 0;
	std::unique_ptr<SentCodeCall> call;

	QString error;

};

struct Form;

struct Value {
	enum class Type {
		PersonalDetails,
		Passport,
		DriverLicense,
		IdentityCard,
		InternalPassport,
		Address,
		UtilityBill,
		BankStatement,
		RentalAgreement,
		PassportRegistration,
		TemporaryRegistration,
		Phone,
		Email,
	};


	explicit Value(Type type);
	Value(Value &&other) = default;

	// Some data is not parsed from server-provided values.
	// It should be preserved through re-parsing (for example when saving).
	// So we hide "operator=(Value&&)" in private and instead provide this.
	void fillDataFrom(Value &&other);
	bool requiresSpecialScan(FileType type) const;
	bool requiresScan(FileType type) const;
	bool scansAreFilled() const;
	void saveInEdit(not_null<Main::Session*> session);
	void clearEditData();
	bool uploadingScan() const;
	bool saving() const;

	static constexpr auto kNothingFilled = 0x100;
	static constexpr auto kNoTranslationFilled = 0x10;
	static constexpr auto kNoSelfieFilled = 0x001;
	int whatNotFilled() const;

	std::vector<File> &files(FileType type);
	const std::vector<File> &files(FileType type) const;
	QString &fileMissingError(FileType type);
	const QString &fileMissingError(FileType type) const;
	std::vector<EditFile> &filesInEdit(FileType type);
	const std::vector<EditFile> &filesInEdit(FileType type) const;
	EditFile &fileInEdit(FileType type, std::optional<int> fileIndex);
	const EditFile &fileInEdit(
		FileType type,
		std::optional<int> fileIndex) const;

	std::vector<EditFile> takeAllFilesInEdit();

	Type type;
	ValueData data;
	std::map<FileType, File> specialScans;
	QString error;
	std::map<FileType, EditFile> specialScansInEdit;
	Verification verification;
	bytes::vector submitHash;

	bool selfieRequired = false;
	bool translationRequired = false;
	bool nativeNames = false;
	int editScreens = 0;

	mtpRequestId saveRequestId = 0;

private:
	Value &operator=(Value &&other) = default;

	std::vector<File> _scans;
	std::vector<File> _translations;
	std::vector<EditFile> _scansInEdit;
	std::vector<EditFile> _translationsInEdit;
	QString _scanMissingError;
	QString _translationMissingError;

};

bool ValueChanged(not_null<const Value*> value, const ValueMap &data);

struct RequestedValue {
	explicit RequestedValue(Value::Type type);

	Value::Type type;
	bool selfieRequired = false;
	bool translationRequired = false;
	bool nativeNames = false;
};

struct RequestedRow {
	std::vector<RequestedValue> values;
};

struct Form {
	using Request = std::vector<std::vector<Value::Type>>;

	std::map<Value::Type, Value> values;
	Request request;
	QString privacyPolicyUrl;
	QVector<MTPSecureValueError> pendingErrors;
};

struct PasswordSettings {
	Core::CloudPasswordCheckRequest request;
	Core::CloudPasswordAlgo newAlgo;
	Core::SecureSecretAlgo newSecureAlgo;
	QString hint;
	QString unconfirmedPattern;
	QString confirmedEmail;
	bool hasRecovery = false;
	bool notEmptyPassport = false;
	bool unknownAlgo = false;

	bool operator==(const PasswordSettings &other) const {
		return (request == other.request)
// newAlgo and newSecureAlgo are always different, because they have
// different random parts added on the client to the server salts.
//			&& (newAlgo == other.newAlgo)
//			&& (newSecureAlgo == other.newSecureAlgo)
			&& ((v::is_null(newAlgo) && v::is_null(other.newAlgo))
				|| (!v::is_null(newAlgo) && !v::is_null(other.newAlgo)))
			&& ((v::is_null(newSecureAlgo) && v::is_null(other.newSecureAlgo))
				|| (!v::is_null(newSecureAlgo)
					&& !v::is_null(other.newSecureAlgo)))
			&& (hint == other.hint)
			&& (unconfirmedPattern == other.unconfirmedPattern)
			&& (confirmedEmail == other.confirmedEmail)
			&& (hasRecovery == other.hasRecovery)
			&& (unknownAlgo == other.unknownAlgo);
	}
	bool operator!=(const PasswordSettings &other) const {
		return !(*this == other);
	}
};

struct FileKey {
	uint64 id = 0;
	int32 dcId = 0;

	inline bool operator==(const FileKey &other) const {
		return (id == other.id) && (dcId == other.dcId);
	}
	inline bool operator!=(const FileKey &other) const {
		return !(*this == other);
	}
	inline bool operator<(const FileKey &other) const {
		return (id < other.id) || ((id == other.id) && (dcId < other.dcId));
	}
	inline bool operator>(const FileKey &other) const {
		return (other < *this);
	}
	inline bool operator<=(const FileKey &other) const {
		return !(other < *this);
	}
	inline bool operator>=(const FileKey &other) const {
		return !(*this < other);
	}

};

class FormController : public base::has_weak_ptr {
public:
	FormController(
		not_null<Window::SessionController*> controller,
		const FormRequest &request);

	[[nodiscard]] not_null<Window::SessionController*> window() const {
		return _controller;
	}
	[[nodiscard]] Main::Session &session() const;

	void show();
	UserData *bot() const;
	QString privacyPolicyUrl() const;
	std::vector<not_null<const Value*>> submitGetErrors();
	void submitPassword(const QByteArray &password);
	void recoverPassword();
	rpl::producer<QString> passwordError() const;
	const PasswordSettings &passwordSettings() const;
	void reloadPassword();
	void reloadAndSubmitPassword(const QByteArray &password);
	void cancelPassword();

	bool canAddScan(not_null<const Value*> value, FileType type) const;
	void uploadScan(
		not_null<const Value*> value,
		FileType type,
		QByteArray &&content);
	void deleteScan(
		not_null<const Value*> value,
		FileType type,
		std::optional<int> fileIndex);
	void restoreScan(
		not_null<const Value*> value,
		FileType type,
		std::optional<int> fileIndex);

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;

	rpl::producer<not_null<const EditFile*>> scanUpdated() const;
	rpl::producer<not_null<const Value*>> valueSaveFinished() const;
	rpl::producer<not_null<const Value*>> verificationNeeded() const;
	rpl::producer<not_null<const Value*>> verificationUpdate() const;
	void verify(not_null<const Value*> value, const QString &code);

	const Form &form() const;
	void startValueEdit(not_null<const Value*> value);
	void cancelValueEdit(not_null<const Value*> value);
	void cancelValueVerification(not_null<const Value*> value);
	void saveValueEdit(not_null<const Value*> value, ValueMap &&data);
	void deleteValueEdit(not_null<const Value*> value);

	void cancel();
	void cancelSure();

	rpl::lifetime &lifetime();

	~FormController();

private:
	using PasswordCheckCallback = Fn<void(
		const Core::CloudPasswordResult &check)>;

	struct FinalData {
		QVector<MTPSecureValueHash> hashes;
		QByteArray credentials;
		std::vector<not_null<const Value*>> errors;
	};

	template <typename Condition>
	EditFile *findEditFileByCondition(Condition &&condition);
	EditFile *findEditFile(const FullMsgId &fullId);
	EditFile *findEditFile(const FileKey &key);
	std::pair<Value*, File*> findFile(const FileKey &key);
	not_null<Value*> findValue(not_null<const Value*> value);

	void requestForm();
	void requestPassword();
	void requestConfig();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const QString &error);
	bool parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();
	Value parseValue(
		const MTPSecureValue &value,
		const std::vector<EditFile> &editData = {}) const;
	std::vector<File> parseFiles(
		const QVector<MTPSecureFile> &data,
		const std::vector<EditFile> &editData) const;
	std::optional<File> parseFile(
		const MTPSecureFile &data,
		const std::vector<EditFile> &editData) const;
	void fillDownloadedFile(
		File &destination,
		const std::vector<EditFile> &source) const;
	bool handleAppUpdateError(const QString &error);

	void submitPassword(
		const Core::CloudPasswordResult &check,
		const QByteArray &password,
		bool submitSaved);
	void checkPasswordHash(
		mtpRequestId &guard,
		bytes::vector hash,
		PasswordCheckCallback callback);
	bool handleSrpIdInvalid(mtpRequestId &guard);
	void requestPasswordData(mtpRequestId &guard);
	void passwordChecked();
	void passwordServerError();
	void passwordDone(const MTPaccount_Password &result);
	bool applyPassword(const MTPDaccount_password &settings);
	bool applyPassword(PasswordSettings &&settings);
	bytes::vector passwordHashForAuth(bytes::const_span password) const;
	void checkSavedPasswordSettings(const SavedCredentials &credentials);
	void checkSavedPasswordSettings(
		const Core::CloudPasswordResult &check,
		const SavedCredentials &credentials);
	void validateSecureSecret(
		bytes::const_span encryptedSecret,
		bytes::const_span passwordHashForSecret,
		bytes::const_span passwordBytes,
		uint64 serverSecretId);
	void decryptValues();
	void decryptValue(Value &value) const;
	bool validateValueSecrets(Value &value) const;
	void resetValue(Value &value) const;
	void fillErrors();
	void fillNativeFromFallback();

	void loadFile(File &file);
	void fileLoadDone(FileKey key, const QByteArray &bytes);
	void fileLoadProgress(FileKey key, int offset);
	void fileLoadFail(FileKey key);
	void generateSecret(bytes::const_span password);
	void saveSecret(
		const Core::CloudPasswordResult &check,
		const SavedCredentials &saved,
		const bytes::vector &secret);

	void subscribeToUploader();
	void encryptFile(
		EditFile &file,
		QByteArray &&content,
		Fn<void(UploadScanData &&result)> callback);
	void prepareFile(
		EditFile &file,
		const QByteArray &content);
	void uploadEncryptedFile(
		EditFile &file,
		UploadScanData &&data);
	void scanUploadDone(const Storage::UploadSecureDone &data);
	void scanUploadProgress(const Storage::UploadSecureProgress &data);
	void scanUploadFail(const FullMsgId &fullId);
	void scanDeleteRestore(
		not_null<const Value*> value,
		FileType type,
		std::optional<int> fileIndex,
		bool deleted);

	QString getPhoneFromValue(not_null<const Value*> value) const;
	QString getEmailFromValue(not_null<const Value*> value) const;
	QString getPlainTextFromValue(not_null<const Value*> value) const;
	void startPhoneVerification(not_null<Value*> value);
	void startEmailVerification(not_null<Value*> value);
	void valueSaveShowError(not_null<Value*> value, const MTP::Error &error);
	void valueSaveFailed(not_null<Value*> value);
	void requestPhoneCall(not_null<Value*> value);
	void verificationError(
		not_null<Value*> value,
		const QString &text);
	void valueEditFailed(not_null<Value*> value);
	void clearValueEdit(not_null<Value*> value);
	void clearValueVerification(not_null<Value*> value);

	bool isEncryptedValue(Value::Type type) const;
	void saveEncryptedValue(not_null<Value*> value);
	void savePlainTextValue(not_null<Value*> value);
	void sendSaveRequest(
		not_null<Value*> value,
		const MTPInputSecureValue &data);
	FinalData prepareFinalData();

	void suggestReset(bytes::vector password);
	void resetSecret(
		const Core::CloudPasswordResult &check,
		const bytes::vector &password);
	void suggestRestart();
	void cancelAbort();
	void shortPollEmailConfirmation();

	not_null<Window::SessionController*> _controller;
	MTP::Sender _api;
	FormRequest _request;
	UserData *_bot = nullptr;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;
	mtpRequestId _configRequestId = 0;

	PasswordSettings _password;
	crl::time _lastSrpIdInvalidTime = 0;
	bytes::vector _passwordCheckHash;
	PasswordCheckCallback _passwordCheckCallback;
	QByteArray _savedPasswordValue;
	Form _form;
	bool _cancelled = false;
	mtpRequestId _recoverRequestId = 0;
	base::flat_map<FileKey, std::unique_ptr<mtpFileLoader>> _fileLoaders;

	rpl::event_stream<not_null<const EditFile*>> _scanUpdated;
	rpl::event_stream<not_null<const Value*>> _valueSaveFinished;
	rpl::event_stream<not_null<const Value*>> _verificationNeeded;
	rpl::event_stream<not_null<const Value*>> _verificationUpdate;

	bytes::vector _secret;
	uint64 _secretId = 0;
	std::vector<Fn<void()>> _secretCallbacks;
	mtpRequestId _saveSecretRequestId = 0;
	rpl::event_stream<> _secretReady;
	rpl::event_stream<QString> _passwordError;
	mtpRequestId _submitRequestId = 0;
	bool _submitSuccess = false;
	bool _suggestingRestart = false;
	QString _serviceErrorText;
	base::Timer _shortPollTimer;

	rpl::lifetime _uploaderSubscriptions;
	rpl::lifetime _lifetime;

	std::unique_ptr<ViewController> _view;

};

} // namespace Passport
