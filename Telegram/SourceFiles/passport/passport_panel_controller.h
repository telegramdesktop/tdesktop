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

struct ScanInfo {
	FileKey key;
	QString status;
	QImage thumb;
	bool deleted = false;

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

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	void uploadScan(QByteArray &&content);
	void deleteScan(int fileIndex);
	void restoreScan(int fileIndex);
	rpl::producer<ScanInfo> scanUpdated() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;

	void showAskPassword() override;
	void showNoPassword() override;
	void showPasswordUnconfirmed() override;

	void fillRows(
		base::lambda<void(
			QString title,
			QString description,
			bool ready)> callback);

	void editScope(int index) override;
	void saveScope(ValueMap &&data, ValueMap &&filesData);
	bool editScopeChanged(
		const ValueMap &data,
		const ValueMap &filesData) const;
	void cancelEditScope();

	void showBox(object_ptr<BoxContent> box) override;

	void cancelAuth();

	rpl::lifetime &lifetime();

private:
	void ensurePanelCreated();

	void cancelValueEdit();
	std::vector<ScanInfo> valueFiles(const Value &value) const;
	void processValueSaveFinished(not_null<const Value*> value);
	void processVerificationNeeded(not_null<const Value*> value);

	ScanInfo collectScanInfo(const EditFile &file) const;
	QString getDefaultContactValue(Scope::Type type) const;

	not_null<FormController*> _form;
	std::vector<Scope> _scopes;

	std::unique_ptr<Panel> _panel;
	base::lambda<bool()> _panelHasUnsavedChanges;
	BoxPointer _confirmForgetChangesBox;
	Scope *_editScope = nullptr;
	int _editScopeFilesIndex = -1;
	std::map<not_null<const Value*>, BoxPointer> _verificationBoxes;

	rpl::lifetime _lifetime;

};

} // namespace Passport
