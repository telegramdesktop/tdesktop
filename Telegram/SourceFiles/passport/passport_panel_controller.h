/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "passport/passport_form_view_controller.h"
#include "passport/passport_form_controller.h"
#include "ui/layers/layer_widget.h"

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Passport {

class FormController;
class Panel;

struct EditDocumentScheme;
struct EditContactScheme;

enum class ReadScanError;

EditDocumentScheme GetDocumentScheme(
	Scope::Type type,
	std::optional<Value::Type> scansType,
	bool nativeNames);
EditContactScheme GetContactScheme(Scope::Type type);

const std::map<QString, QString> &LatinToNativeMap();
const std::map<QString, QString> &NativeToLatinMap();
QString AdjustKeyName(not_null<const Value*> value, const QString &key);
bool SkipFieldCheck(not_null<const Value*> value, const QString &key);

struct ScanInfo {
	explicit ScanInfo(FileType type);
	ScanInfo(
		FileType type,
		const FileKey &key,
		const QString &status,
		const QImage &thumb,
		bool deleted,
		const QString &error);

	FileType type;
	FileKey key;
	QString status;
	QImage thumb;
	bool deleted = false;
	QString error;
};

struct ScopeError {
	enum class General {
		WholeValue,
		ScanMissing,
		TranslationMissing,
	};

	// FileKey - file_hash error (bad scan / selfie / translation)
	// General - general value error (or scan / translation missing)
	// QString - data_hash with such key error (bad value)
	std::variant<FileKey, General, QString> key;
	QString text;
};

class PanelController : public ViewController {
public:
	PanelController(not_null<FormController*> form);

	not_null<UserData*> bot() const;
	QString privacyPolicyUrl() const;
	void submitForm();
	void submitPassword(const QByteArray &password);
	void recoverPassword();
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;
	QString unconfirmedEmailPattern() const;

	void setupPassword();
	void cancelPasswordSubmit();
	void validateRecoveryEmail();

	bool canAddScan(FileType type) const;
	void uploadScan(FileType type, QByteArray &&content);
	void deleteScan(FileType type, std::optional<int> fileIndex);
	void restoreScan(FileType type, std::optional<int> fileIndex);
	rpl::producer<ScanInfo> scanUpdated() const;
	rpl::producer<ScopeError> saveErrors() const;
	void readScanError(ReadScanError error);

	std::optional<rpl::producer<QString>> deleteValueLabel() const;
	void deleteValue();

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;

	void showAskPassword() override;
	void showNoPassword() override;
	void showCriticalError(const QString &error) override;
	void showUpdateAppBox() override;

	void fillRows(
		Fn<void(
			QString title,
			QString description,
			bool ready,
			bool error)> callback);
	rpl::producer<> refillRows() const;

	void editScope(int index) override;
	void saveScope(ValueMap &&data, ValueMap &&filesData);
	bool editScopeChanged(
		const ValueMap &data,
		const ValueMap &filesData) const;
	void cancelEditScope();

	void showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) override;
	void showToast(const QString &text) override;
	void suggestReset(Fn<void()> callback) override;

	int closeGetDuration() override;

	void cancelAuth();
	void cancelAuthSure();

	rpl::lifetime &lifetime();

	~PanelController();

private:
	void ensurePanelCreated();

	void editScope(int index, std::optional<int> documentIndex);
	void editWithUpload(int index, int documentIndex);
	bool editRequiresScanUpload(
		int index,
		std::optional<int> documentIndex) const;
	void startScopeEdit(int index, std::optional<int> documentIndex);
	std::optional<int> findBestDocumentIndex(const Scope &scope) const;
	void requestScopeFilesType(int index);
	void cancelValueEdit();
	void processValueSaveFinished(not_null<const Value*> value);
	void processVerificationNeeded(not_null<const Value*> value);

	bool savingScope() const;
	bool uploadingScopeScan() const;
	bool hasValueDocument() const;
	bool hasValueFields() const;
	std::vector<ScopeError> collectSaveErrors(
		not_null<const Value*> value) const;
	QString getDefaultContactValue(Scope::Type type) const;
	void deleteValueSure(bool withDetails);

	void resetPassport(Fn<void()> callback);
	void cancelReset();

	not_null<FormController*> _form;
	std::vector<Scope> _scopes;
	rpl::event_stream<> _submitFailed;
	std::vector<not_null<const Value*>> _submitErrors;
	rpl::event_stream<ScopeError> _saveErrors;

	std::unique_ptr<Panel> _panel;
	Fn<bool()> _panelHasUnsavedChanges;
	QPointer<Ui::BoxContent> _confirmForgetChangesBox;
	std::vector<Ui::BoxPointer> _editScopeBoxes;
	Scope *_editScope = nullptr;
	const Value *_editValue = nullptr;
	const Value *_editDocument = nullptr;
	Ui::BoxPointer _scopeDocumentTypeBox;
	std::map<not_null<const Value*>, Ui::BoxPointer> _verificationBoxes;

	Ui::BoxPointer _resetBox;

	rpl::lifetime _lifetime;

};

} // namespace Passport
