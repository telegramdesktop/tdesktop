/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "smartglocal/smartglocal_error.h"

namespace SmartGlocal {

Error::Code Error::code() const {
	return _code;
}

QString Error::description() const {
	return _description;
}

QString Error::message() const {
	return _message;
}

QString Error::parameter() const {
	return _parameter;
}

Error Error::None() {
	return Error(Code::None, {}, {}, {});
}

Error Error::DecodedObjectFromResponse(QJsonObject object) {
	if (object.value("status").toString() == "ok") {
		return Error::None();
	}
	const auto entry = object.value("error");
	if (!entry.isObject()) {
		return {
			Code::Unknown,
			"GenericError",
			"Could not read the error response "
			"that was returned from SmartGlocal."
		};
	}
	const auto error = entry.toObject();
	const auto string = [&](QStringView key) {
		return error.value(key).toString();
	};
	const auto code = string(u"code");
	const auto description = string(u"description");

	// There should always be a message and type for the error
	if (code.isEmpty() || description.isEmpty()) {
		return {
			Code::Unknown,
			"GenericError",
			"Could not interpret the error response "
			"that was returned from SmartGlocal."
		};
	}

	return { Code::Unknown, code, description };
}

bool Error::empty() const {
	return (_code == Code::None);
}

} // namespace SmartGlocal
