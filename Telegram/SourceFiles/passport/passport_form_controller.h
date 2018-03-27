/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/weak_ptr.h"

class BoxContent;

namespace Storage {
struct UploadedSecure;
} // namespace Storage

namespace Window {
class Controller;
} // namespace Window

namespace Passport {

struct FormRequest {
	FormRequest(
		UserId botId,
		const QString &scope,
		const QString &callbackUrl,
		const QString &publicKey);

	UserId botId;
	QString scope;
	QString callbackUrl;
	QString publicKey;

};

struct IdentityData;

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

struct ScanInfo {
	FileKey key;
	QString date;
	QImage thumb;

};

class FormController : private MTP::Sender, public base::has_weak_ptr {
public:
	FormController(
		not_null<Window::Controller*> controller,
		const FormRequest &request);

	void show();

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	void uploadScan(int index, QByteArray &&content);

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;
	rpl::producer<ScanInfo> scanUpdated() const;

	void fillRows(
		base::lambda<void(
			QString title,
			QString description,
			bool ready)> callback);
	void editField(int index);

	void saveFieldIdentity(int index, const IdentityData &data);

	~FormController();

private:
	struct UploadedScan {
		~UploadedScan();

		FullMsgId fullId;
		uint64 fileId = 0;
		int partsCount = 0;
		QByteArray md5checksum;
		bytes::vector hash;
		bytes::vector bytes;
	};
	struct File {
		uint64 id = 0;
		uint64 accessHash = 0;
		int32 size = 0;
		int32 dcId = 0;
		bytes::vector fileHash;
		bytes::vector bytes;
	};
	struct EditFile {
		EditFile(
			const File &fields,
			std::unique_ptr<UploadedScan> &&uploaded);

		File fields;
		std::unique_ptr<UploadedScan> uploaded;
	};
	struct Verification {
		TimeId date;
		QString provider;
	};
	struct Field {
		enum class Type {
			Identity,
			Address,
			Phone,
			Email,
		};

		explicit Field(Type type);
		Field(Field &&other) = default;
		Field &operator=(Field &&other) = default;

		Type type;
		QByteArray originalData;
		std::map<QString, QString> parsedData;
		bytes::vector dataHash;
		std::vector<File> files;
		std::vector<EditFile> filesInEdit;
		bytes::vector secret;
		bytes::vector encryptedSecret;
		bytes::vector hash;
		base::optional<Verification> verification;
	};
	struct Form {
		bool requestWrite = false;
		std::vector<Field> fields;
	};
	struct PasswordSettings {
		bytes::vector salt;
		bytes::vector newSalt;
		bytes::vector newSecureSalt;
		QString hint;
		QString unconfirmedPattern;
		QString confirmedEmail;
		bool hasRecovery = false;
	};

	EditFile *findEditFile(const FullMsgId &fullId);
	std::pair<Field*, File*> findFile(const FileKey &key);

	void requestForm();
	void requestPassword();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const RPCError &error);
	void parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();
	Field parseValue(const MTPSecureValue &value) const;
	template <typename DataType>
	Field parseEncryptedField(
		Field::Type type,
		const DataType &data) const;
	template <typename DataType>
	Field parsePlainTextField(
		Field::Type type,
		const QByteArray &value,
		const DataType &data) const;
	Verification parseVerified(const MTPSecureValueVerified &data) const;

	void passwordDone(const MTPaccount_Password &result);
	void passwordFail(const RPCError &error);
	void parsePassword(const MTPDaccount_noPassword &settings);
	void parsePassword(const MTPDaccount_password &settings);
	bytes::vector passwordHashForAuth(bytes::const_span password) const;
	void validateSecureSecret(
		bytes::const_span salt,
		bytes::const_span encryptedSecret,
		bytes::const_span password);
	void decryptFields();
	void decryptField(Field &field);
	bool validateFieldSecret(Field &field);
	void resetField(Field &field);

	IdentityData fieldDataIdentity(const Field &field) const;
	std::vector<ScanInfo> fieldFilesIdentity(const Field &field) const;
	void saveIdentity(int index);

	void loadFiles(const std::vector<File> &files);
	void fileLoaded(FileKey key, const QByteArray &bytes);
	void generateSecret(bytes::const_span password);

	template <typename FileHashes>
	bytes::vector computeFilesHash(
		FileHashes fileHashes,
		bytes::const_span valueHash);

	void subscribeToUploader();
	void uploadEncryptedScan(int index, UploadedScan &&data);
	void scanUploaded(const Storage::UploadedSecure &data);

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;

	PasswordSettings _password;
	Form _form;
	std::map<FileKey, std::unique_ptr<mtpFileLoader>> _fileLoaders;
	rpl::event_stream<ScanInfo> _scanUpdated;

	bytes::vector _secret;
	std::vector<base::lambda<void()>> _secretCallbacks;
	mtpRequestId _saveSecretRequestId = 0;
	rpl::event_stream<> _secretReady;
	rpl::event_stream<QString> _passwordError;

	QPointer<BoxContent> _editBox;

	rpl::lifetime _uploaderSubscriptions;

};

} // namespace Passport
