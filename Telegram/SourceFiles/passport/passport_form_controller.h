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

class BoxContent;

namespace Storage {
struct UploadSecureDone;
struct UploadSecureProgress;
} // namespace Storage

namespace Window {
class Controller;
} // namespace Window

namespace Passport {

class ViewController;

struct FormRequest {
	FormRequest(
		UserId botId,
		const QString &scope,
		const QString &callbackUrl,
		const QString &publicKey,
		const QString &payload,
		const QString &errors);

	UserId botId;
	QString scope;
	QString callbackUrl;
	QString publicKey;
	QString payload;
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
	UploadScanDataPointer(std::unique_ptr<UploadScanData> &&value);
	UploadScanDataPointer(UploadScanDataPointer &&other);
	UploadScanDataPointer &operator=(UploadScanDataPointer &&other);
	~UploadScanDataPointer();

	UploadScanData *get() const;
	operator UploadScanData*() const;
	explicit operator bool() const;
	UploadScanData *operator->() const;

private:
	std::unique_ptr<UploadScanData> _value;

};

struct Value;

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
		not_null<const Value*> value,
		const File &fields,
		std::unique_ptr<UploadScanData> &&uploadData);

	not_null<const Value*> value;
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

enum class SpecialFile {
	FrontSide,
	ReverseSide,
	Selfie,
};

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
	Value &operator=(Value &&other) = default;

	bool requiresSpecialScan(SpecialFile type, bool selfieRequired) const;
	bool scansAreFilled(bool selfieRequired) const;

	Type type;
	ValueData data;
	std::vector<File> scans;
	std::map<SpecialFile, File> specialScans;
	QString scanMissingError;
	std::vector<EditFile> scansInEdit;
	std::map<SpecialFile, EditFile> specialScansInEdit;
	Verification verification;
	bytes::vector submitHash;

	int editScreens = 0;
	mtpRequestId saveRequestId = 0;

};

struct Form {
	std::map<Value::Type, Value> values;
	std::vector<Value::Type> request;
	bool identitySelfieRequired = false;
	QString privacyPolicyUrl;
	QVector<MTPSecureValueError> pendingErrors;

};

struct PasswordSettings {
	bytes::vector salt;
	bytes::vector newSalt;
	bytes::vector newSecureSalt;
	QString hint;
	QString unconfirmedPattern;
	QString confirmedEmail;
	bool hasRecovery = false;
	bool notEmptyPassport = false;

	bool operator==(const PasswordSettings &other) const {
		return (salt == other.salt)
			&& (newSalt == other.newSalt)
			&& (newSecureSalt == other.newSecureSalt)
			&& (hint == other.hint)
			&& (unconfirmedPattern == other.unconfirmedPattern)
			&& (confirmedEmail == other.confirmedEmail)
			&& (hasRecovery == other.hasRecovery);
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

class FormController : private MTP::Sender, public base::has_weak_ptr {
public:
	FormController(
		not_null<Window::Controller*> controller,
		const FormRequest &request);

	void show();
	UserData *bot() const;
	QString privacyPolicyUrl() const;
	std::vector<not_null<const Value*>> submitGetErrors();
	void submitPassword(const QString &password);
	void recoverPassword();
	rpl::producer<QString> passwordError() const;
	const PasswordSettings &passwordSettings() const;
	void reloadPassword();
	void cancelPassword();

	bool canAddScan(not_null<const Value*> value) const;
	void uploadScan(not_null<const Value*> value, QByteArray &&content);
	void deleteScan(not_null<const Value*> value, int fileIndex);
	void restoreScan(not_null<const Value*> value, int fileIndex);
	void uploadSpecialScan(
		not_null<const Value*> value,
		SpecialFile type,
		QByteArray &&content);
	void deleteSpecialScan(
		not_null<const Value*> value,
		SpecialFile type);
	void restoreSpecialScan(
		not_null<const Value*> value,
		SpecialFile type);

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
	bool editValueChanged(
		not_null<const Value*> value,
		const ValueMap &data) const;
	void saveValueEdit(not_null<const Value*> value, ValueMap &&data);
	void deleteValueEdit(not_null<const Value*> value);
	bool savingValue(not_null<const Value*> value) const;
	bool uploadingScan(not_null<const Value*> value) const;

	void cancel();
	void cancelSure();

	rpl::lifetime &lifetime();

	~FormController();

private:
	struct FinalData {
		QVector<MTPSecureValueHash> hashes;
		QByteArray credentials;
		std::vector<not_null<const Value*>> errors;
	};
	EditFile *findEditFile(const FullMsgId &fullId);
	EditFile *findEditFile(const FileKey &key);
	std::pair<Value*, File*> findFile(const FileKey &key);
	not_null<Value*> findValue(not_null<const Value*> value);

	void requestForm();
	void requestPassword();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const QString &error);
	void parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();
	Value parseValue(
		const MTPSecureValue &value,
		const std::vector<EditFile> &editData = {}) const;
	std::vector<File> parseFiles(
		const QVector<MTPSecureFile> &data,
		const std::vector<EditFile> &editData) const;
	base::optional<File> parseFile(
		const MTPSecureFile &data,
		const std::vector<EditFile> &editData) const;
	void fillDownloadedFile(
		File &destination,
		const std::vector<EditFile> &source) const;

	void passwordDone(const MTPaccount_Password &result);
	bool applyPassword(const MTPDaccount_noPassword &settings);
	bool applyPassword(const MTPDaccount_password &settings);
	bool applyPassword(PasswordSettings &&settings);
	bytes::vector passwordHashForAuth(bytes::const_span password) const;
	void validateSecureSecret(
		bytes::const_span salt,
		bytes::const_span encryptedSecret,
		bytes::const_span password,
		uint64 serverSecretId);
	void decryptValues();
	void decryptValue(Value &value);
	bool validateValueSecrets(Value &value);
	void resetValue(Value &value);
	void fillErrors();

	void loadFile(File &file);
	void fileLoadDone(FileKey key, const QByteArray &bytes);
	void fileLoadProgress(FileKey key, int offset);
	void fileLoadFail(FileKey key);
	void generateSecret(bytes::const_span password);

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
		int fileIndex,
		bool deleted);
	void specialScanDeleteRestore(
		not_null<const Value*> value,
		SpecialFile type,
		bool deleted);

	QString getPhoneFromValue(not_null<const Value*> value) const;
	QString getEmailFromValue(not_null<const Value*> value) const;
	QString getPlainTextFromValue(not_null<const Value*> value) const;
	void startPhoneVerification(not_null<Value*> value);
	void startEmailVerification(not_null<Value*> value);
	void valueSaveShowError(not_null<Value*> value, const RPCError &error);
	void valueSaveFailed(not_null<Value*> value);
	void requestPhoneCall(not_null<Value*> value);
	void verificationError(
		not_null<Value*> value,
		const QString &text);
	void valueEditFailed(not_null<Value*> value);
	void clearValueEdit(not_null<Value*> value);
	void clearValueVerification(not_null<Value*> value);
	bool editFileChanged(const EditFile &file) const;

	bool isEncryptedValue(Value::Type type) const;
	void saveEncryptedValue(not_null<Value*> value);
	void savePlainTextValue(not_null<Value*> value);
	void sendSaveRequest(
		not_null<Value*> value,
		const MTPInputSecureValue &data);
	FinalData prepareFinalData();

	void suggestReset(bytes::vector password);
	void suggestRestart();
	void cancelAbort();
	void shortPollEmailConfirmation();

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;

	PasswordSettings _password;
	Form _form;
	bool _cancelled = false;
	mtpRequestId _recoverRequestId = 0;
	std::map<FileKey, std::unique_ptr<mtpFileLoader>> _fileLoaders;

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
