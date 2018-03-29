/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_view_separate.h"

#include "lang/lang_keys.h"
#include "passport/passport_edit_identity_box.h"
#include "passport/passport_form_box.h"
#include "boxes/confirm_box.h"

namespace Passport {

BoxPointer::BoxPointer(QPointer<BoxContent> value)
: _value(value) {
}

BoxPointer::BoxPointer(BoxPointer &&other)
: _value(base::take(other._value)) {
}

BoxPointer &BoxPointer::operator=(BoxPointer &&other) {
	std::swap(_value, other._value);
	return *this;
}

BoxPointer::~BoxPointer() {
	if (const auto strong = get()) {
		strong->closeBox();
	}
}

BoxContent *BoxPointer::get() const {
	return _value.data();
}

BoxPointer::operator BoxContent*() const {
	return get();
}

BoxPointer::operator bool() const {
	return get();
}

BoxContent *BoxPointer::operator->() const {
	return get();
}

ViewSeparate::ViewSeparate(not_null<FormController*> form)
: _form(form) {
}

not_null<UserData*> ViewSeparate::bot() const {
	return _form->bot();
}

void ViewSeparate::fillRows(
	base::lambda<void(
		QString title,
		QString description,
		bool ready)> callback) {
	_form->enumerateRows([&](const Value &value) {
		switch (value.type) {
		case Value::Type::Identity:
			callback(
				lang(lng_passport_identity_title),
				lang(lng_passport_identity_description),
				false);
			break;
		case Value::Type::Address:
			callback(
				lang(lng_passport_address_title),
				lang(lng_passport_address_description),
				false);
			break;
		case Value::Type::Phone:
			callback(
				lang(lng_passport_phone_title),
				App::self()->phone(),
				true);
			break;
		case Value::Type::Email:
			callback(
				lang(lng_passport_email_title),
				lang(lng_passport_email_description),
				false);
			break;
		}
	});
}

void ViewSeparate::submitPassword(const QString &password) {
	_form->submitPassword(password);
}

rpl::producer<QString> ViewSeparate::passwordError() const {
	return _form->passwordError();
}

QString ViewSeparate::passwordHint() const {
	return _form->passwordHint();
}

rpl::producer<> ViewSeparate::secretReadyEvents() const {
	return _form->secretReadyEvents();
}

QString ViewSeparate::defaultEmail() const {
	return _form->defaultEmail();
}

QString ViewSeparate::defaultPhoneNumber() const {
	return _form->defaultPhoneNumber();
}

void ViewSeparate::uploadScan(int valueIndex, QByteArray &&content) {
	Expects(_editBox != nullptr);

	_form->uploadScan(valueIndex, std::move(content));
}

void ViewSeparate::deleteScan(int valueIndex, int fileIndex) {
	Expects(_editBox != nullptr);

	_form->deleteScan(valueIndex, fileIndex);
}

rpl::producer<ScanInfo> ViewSeparate::scanUpdated() const {
	return _form->scanUpdated(
	) | rpl::map([=](not_null<const EditFile*> file) {
		return collectScanInfo(*file);
	});
}

ScanInfo ViewSeparate::collectScanInfo(const EditFile &file) const {
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
				return QString("uploaded ")
					+ langDateTimeFull(ParseDateTime(file.fields.date));
			}
		} else if (file.uploadData) {
			if (file.uploadData->offset < 0) {
				return QString("upload failed");
			} else if (file.uploadData->fullId) {
				return QString("uploading %1 / %2"
				).arg(file.uploadData->offset
				).arg(file.uploadData->bytes.size());
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

void ViewSeparate::showForm() {
	if (!_form->bot()) {
		Ui::show(Box<InformBox>("Could not get authorization bot."));
		return;
	}
	Ui::show(Box<FormBox>(this));
}

void ViewSeparate::editValue(int index) {
	_editValue = _form->startValueEdit(index);
	Assert(_editValue != nullptr);

	auto box = [&]() -> object_ptr<BoxContent> {
		switch (_editValue->type) {
		case Value::Type::Identity:
			return Box<IdentityBox>(
				this,
				index,
				valueDataIdentity(*_editValue),
				valueFiles(*_editValue));
		}
		return { nullptr };
	}();
	if (box) {
		_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
		_editBox->boxClosing() | rpl::start_with_next([=] {
			cancelValueEdit(index);
		}, _form->lifetime());
	} else {
		cancelValueEdit(index);
	}
}

IdentityData ViewSeparate::valueDataIdentity(const Value &value) const {
	const auto &map = value.data.parsed;
	auto result = IdentityData();
	if (const auto i = map.find(qsl("first_name")); i != map.cend()) {
		result.name = i->second;
	}
	if (const auto i = map.find(qsl("last_name")); i != map.cend()) {
		result.surname = i->second;
	}
	return result;
}

std::vector<ScanInfo> ViewSeparate::valueFiles(const Value &value) const {
	auto result = std::vector<ScanInfo>();
	for (const auto &file : value.filesInEdit) {
		result.push_back(collectScanInfo(file));
	}
	return result;
}

void ViewSeparate::cancelValueEdit(int index) {
	if (base::take(_editValue)) {
		_form->cancelValueEdit(index);
	}
}

void ViewSeparate::saveValueIdentity(
		int index,
		const IdentityData &data) {
	Expects(_editBox != nullptr);
	Expects(_editValue != nullptr);
	Expects(_editValue->type == Value::Type::Identity);

	_editValue->data.parsed[qsl("first_name")] = data.name;
	_editValue->data.parsed[qsl("last_name")] = data.surname;
	_editValue = nullptr;

	_editBox->closeBox();

	_form->saveValueEdit(index);
}

} // namespace Passport
