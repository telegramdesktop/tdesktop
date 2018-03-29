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

struct IdentityData;

struct ScanInfo {
	FileKey key;
	QString status;
	QImage thumb;

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

class ViewSeparate : public ViewController {
public:
	ViewSeparate(not_null<FormController*> form);

	not_null<UserData*> bot() const;

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	void uploadScan(int valueIndex, QByteArray &&content);
	void deleteScan(int valueIndex, int fileIndex);
	rpl::producer<ScanInfo> scanUpdated() const;

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;

	void showForm() override;
	void fillRows(
		base::lambda<void(
			QString title,
			QString description,
			bool ready)> callback);

	void editValue(int index) override;
	void saveValueIdentity(int index, const IdentityData &data);

private:
	void cancelValueEdit(int index);
	IdentityData valueDataIdentity(const Value &value) const;
	std::vector<ScanInfo> valueFiles(const Value &value) const;

	ScanInfo collectScanInfo(const EditFile &file) const;

	not_null<FormController*> _form;

	Value *_editValue = nullptr;
	BoxPointer _editBox;

};

} // namespace Passport
