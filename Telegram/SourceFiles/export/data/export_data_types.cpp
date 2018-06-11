/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/data/export_data_types.h"

namespace App { // Hackish..
QString formatPhone(QString phone);
} // namespace App

namespace Export {
namespace Data {

Utf8String ParseString(const MTPstring &data) {
	return data.v;
}

FileLocation ParseLocation(const MTPFileLocation &data) {
	switch (data.type()) {
	case mtpc_fileLocation: {
		const auto &location = data.c_fileLocation();
		return {
			location.vdc_id.v,
			MTP_inputFileLocation(
				location.vvolume_id,
				location.vlocal_id,
				location.vsecret)
		};
	} break;
	case mtpc_fileLocationUnavailable: {
		const auto &location = data.c_fileLocationUnavailable();
		return {
			0,
			MTP_inputFileLocation(
				location.vvolume_id,
				location.vlocal_id,
				location.vsecret)
		};
	} break;
	}
	Unexpected("Type in ParseLocation.");
}

File ParseMaxImage(
		const MTPVector<MTPPhotoSize> &data,
		const QString &suggestedPath) {
	auto result = File();
	result.suggestedPath = suggestedPath;

	auto maxArea = int64(0);
	for (const auto &size : data.v) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const auto &fields = size.c_photoSize();
			const auto area = fields.vw.v * int64(fields.vh.v);
			if (area > maxArea) {
				result.location = ParseLocation(fields.vlocation);
				result.size = fields.vsize.v;
				result.content = QByteArray();
				maxArea = area;
			}
		} break;

		case mtpc_photoCachedSize: {
			const auto &fields = size.c_photoCachedSize();
			const auto area = fields.vw.v * int64(fields.vh.v);
			if (area > maxArea) {
				result.location = ParseLocation(fields.vlocation);
				result.size = fields.vbytes.v.size();
				result.content = fields.vbytes.v;
				maxArea = area;
			}
		} break;
		}
	}
	return result;
}

Photo ParsePhoto(const MTPPhoto &data, const QString &suggestedPath) {
	auto result = Photo();
	switch (data.type()) {
	case mtpc_photo: {
		const auto &photo = data.c_photo();
		result.id = photo.vid.v;
		result.date = QDateTime::fromTime_t(photo.vdate.v);
		result.image = ParseMaxImage(photo.vsizes, suggestedPath);
	} break;

	case mtpc_photoEmpty: {
		const auto &photo = data.c_photoEmpty();
		result.id = photo.vid.v;
	} break;

	default: Unexpected("Photo type in ParsePhoto.");
	}
	return result;
}

Utf8String FormatDateTime(
		const int32 date,
		QChar dateSeparator,
		QChar timeSeparator,
		QChar separator) {
	const auto value = QDateTime::fromTime_t(date);
	return (QString("%1") + dateSeparator + "%2" + dateSeparator + "%3"
		+ separator + "%4" + timeSeparator + "%5" + timeSeparator + "%6"
	).arg(value.date().year()
	).arg(value.date().month(), 2, 10, QChar('0')
	).arg(value.date().day(), 2, 10, QChar('0')
	).arg(value.time().hour(), 2, 10, QChar('0')
	).arg(value.time().minute(), 2, 10, QChar('0')
	).arg(value.time().second(), 2, 10, QChar('0')
	).toUtf8();
}

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data) {
	const auto &list = data.v;
	auto result = UserpicsSlice();
	result.list.reserve(list.size());
	for (const auto &photo : list) {
		const auto suggestedPath = "PersonalPhotos/Photo_"
			+ (photo.type() == mtpc_photo
				? QString::fromUtf8(
					FormatDateTime(photo.c_photo().vdate.v, '_', '_', '_'))
				: "Empty")
			+ ".jpg";
		result.list.push_back(ParsePhoto(photo, suggestedPath));
	}
	return result;
}

User ParseUser(const MTPUser &data) {
	auto result = User();
	switch (data.type()) {
	case mtpc_user: {
		const auto &fields = data.c_user();
		result.id = fields.vid.v;
		if (fields.has_first_name()) {
			result.firstName = ParseString(fields.vfirst_name);
		}
		if (fields.has_last_name()) {
			result.lastName = ParseString(fields.vlast_name);
		}
		if (fields.has_phone()) {
			result.phoneNumber = ParseString(fields.vphone);
		}
		if (fields.has_username()) {
			result.username = ParseString(fields.vusername);
		}
	} break;

	case mtpc_userEmpty: {
		const auto &fields = data.c_userEmpty();
		result.id = fields.vid.v;
	} break;

	default: Unexpected("Type in ParseUser.");
	}
	return result;
}

std::map<int, User> ParseUsersList(const MTPVector<MTPUser> &data) {
	auto result = std::map<int, User>();
	for (const auto &user : data.v) {
		auto parsed = ParseUser(user);
		result.emplace(parsed.id, std::move(parsed));
	}
	return result;
}

PersonalInfo ParsePersonalInfo(const MTPUserFull &data) {
	Expects(data.type() == mtpc_userFull);

	const auto &fields = data.c_userFull();
	auto result = PersonalInfo();
	result.user = ParseUser(fields.vuser);
	if (fields.has_about()) {
		result.bio = ParseString(fields.vabout);
	}
	return result;
}

ContactsList ParseContactsList(const MTPcontacts_Contacts &data) {
	Expects(data.type() == mtpc_contacts_contacts);

	auto result = ContactsList();
	const auto &contacts = data.c_contacts_contacts();
	const auto map = ParseUsersList(contacts.vusers);
	for (const auto &contact : contacts.vcontacts.v) {
		const auto userId = contact.c_contact().vuser_id.v;
		if (const auto i = map.find(userId); i != end(map)) {
			result.list.push_back(i->second);
		} else {
			result.list.push_back(User());
		}
	}
	return result;
}

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber) {
	return phoneNumber.isEmpty()
		? Utf8String()
		: App::formatPhone(QString::fromUtf8(phoneNumber)).toUtf8();
}

} // namespace Data
} // namespace Export
