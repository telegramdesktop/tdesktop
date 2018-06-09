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

PersonalInfo ParsePersonalInfo(const MTPUserFull &data) {
	Expects(data.type() == mtpc_userFull);

	const auto &fields = data.c_userFull();
	const auto &small = fields.vuser.c_user();
	auto result = PersonalInfo();
	if (small.has_first_name()) {
		result.firstName = ParseString(small.vfirst_name);
	}
	if (small.has_last_name()) {
		result.lastName = ParseString(small.vlast_name);
	}
	if (small.has_phone()) {
		result.phoneNumber = ParseString(small.vphone);
	}
	if (small.has_username()) {
		result.username = ParseString(small.vusername);
	}
	if (fields.has_about()) {
		result.bio = ParseString(fields.vabout);
	}
	return result;
}

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data) {
	const auto &list = data.v;
	auto result = UserpicsSlice();
	result.list.reserve(list.size());
	for (const auto &photo : list) {
		switch (photo.type()) {
		case mtpc_photo: {
			const auto &fields = photo.c_photo();
			auto userpic = Userpic();
			userpic.id = fields.vid.v;
			userpic.date = QDateTime::fromTime_t(fields.vdate.v);
			userpic.image = File{ "(not saved)" };
			result.list.push_back(std::move(userpic));
		} break;

		case mtpc_photoEmpty: {
			const auto &fields = photo.c_photoEmpty();
			auto userpic = Userpic();
			userpic.id = fields.vid.v;
			result.list.push_back(std::move(userpic));
		} break;

		default: Unexpected("Photo type in ParseUserpicsSlice.");
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
