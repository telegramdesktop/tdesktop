/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {

class Session;

class AppConfig final {
public:
	explicit AppConfig(not_null<Session*> session);

	template <typename Type>
	Type get(const QString &key, Type fallback) const {
		if constexpr (std::is_same_v<Type, double>) {
			return getDouble(key, fallback);
		}
	}

private:
	void refresh();
	void refreshDelayed();

	double getDouble(const QString &key, double fallback) const;

	not_null<Session*> _session;
	mtpRequestId _requestId = 0;
	base::flat_map<QString, MTPJSONValue> _data;

};

} // namespace Main
