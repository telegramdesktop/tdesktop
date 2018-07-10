/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "passport/passport_form_view_controller.h"
#include "passport/passport_form_controller.h"

namespace Passport {

class FormController;
class Panel;

struct EditDocumentScheme;
struct EditContactScheme;

enum class ReadScanError;

EditDocumentScheme GetDocumentScheme(
	Scope::Type type,
	base::optional<Value::Type> scansType = base::none);
EditContactScheme GetContactScheme(Scope::Type type);

struct ScanInfo {
	FileKey key;
	QString status;
	QImage thumb;
	bool deleted = false;
	base::optional<SpecialFile> special;
	QString error;

};

struct ScopeError {
	// FileKey:id != 0 - file_hash error (bad scan / selfie)
	// FileKey:id == 0 - vector<file_hash> error (scan missing)
	// QString - data_hash with such key error (bad value)
	base::variant<FileKey, QString> key;
	QString text;

};

class BoxPointer {
public:
	BoxPointer(QPointer<BoxContent> value = nullptr);
	BoxPointer(BoxPointer &&other);
	BoxPointer &operator=(BoxPointer &&other);
	~BoxPointer();

	BoxContent *get() const;
	operator BoxContent*() const;
	explicit operator bool() const;
	BoxContent *operator->() const;

private:
	QPointer<BoxContent> _value;

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

	bool canAddScan() const;
	void uploadScan(QByteArray &&content);
	void deleteScan(int fileIndex);
	void restoreScan(int fileIndex);
	void uploadSpecialScan(SpecialFile type, QByteArray &&content);
	void deleteSpecialScan(SpecialFile type);
	void restoreSpecialScan(SpecialFile type);
	rpl::producer<ScanInfo> scanUpdated() const;
	rpl::producer<ScopeError> saveErrors() const;
	void readScanError(ReadScanError error);

	base::optional<rpl::producer<QString>> deleteValueLabel() const;
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
		object_ptr<BoxContent> box,
		LayerOptions options,
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

	void editScope(int index, int documentIndex);
	void editWithUpload(int index, int documentIndex);
	int findNonEmptyDocumentIndex(const Scope &scope) const;
	void requestScopeFilesType(int index);
	void cancelValueEdit();
	std::vector<ScanInfo> valueFiles(const Value &value) const;
	std::map<SpecialFile, ScanInfo> valueSpecialFiles(
		const Value &value) const;
	void processValueSaveFinished(not_null<const Value*> value);
	void processVerificationNeeded(not_null<const Value*> value);

	bool savingScope() const;
	bool uploadingScopeScan() const;
	bool hasValueDocument() const;
	bool hasValueFields() const;
	ScanInfo collectScanInfo(const EditFile &file) const;
	std::vector<ScopeError> collectErrors(
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
	QPointer<BoxContent> _confirmForgetChangesBox;
	std::vector<BoxPointer> _editScopeBoxes;
	Scope *_editScope = nullptr;
	const Value *_editValue = nullptr;
	const Value *_editDocument = nullptr;
	BoxPointer _scopeDocumentTypeBox;
	std::map<not_null<const Value*>, BoxPointer> _verificationBoxes;

	BoxPointer _resetBox;

	rpl::lifetime _lifetime;

};

} // namespace Passport
