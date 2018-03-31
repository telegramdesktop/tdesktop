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

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	void uploadScan(int valueIndex, QByteArray &&content);
	void deleteScan(int valueIndex, int fileIndex);
	void restoreScan(int valueIndex, int fileIndex);
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

	void editValue(int index) override;
	void saveValue(int index, ValueMap &&data);

	void cancelAuth();

	rpl::lifetime &lifetime();

private:
	void ensurePanelCreated();

	void cancelValueEdit(int index);
	std::vector<ScanInfo> valueFiles(const Value &value) const;

	ScanInfo collectScanInfo(const EditFile &file) const;

	not_null<FormController*> _form;

	std::unique_ptr<Panel> _panel;
	Value *_editValue = nullptr;

	rpl::lifetime _lifetime;

};

} // namespace Passport
