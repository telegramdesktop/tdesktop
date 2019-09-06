/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct CloudTheme {
	uint64 id = 0;
	uint64 accessHash = 0;
	QString slug;
	QString title;
	DocumentId documentId = 0;
	UserId createdBy = 0;

	static CloudTheme Parse(
		not_null<Main::Session*> session,
		const MTPDtheme &data);
};

class CloudThemes final {
public:
	explicit CloudThemes(not_null<Main::Session*> session);

	[[nodiscard]] static QString Format();

	void refresh();
	[[nodiscard]] rpl::producer<> updated() const;
	[[nodiscard]] const std::vector<CloudTheme> &list() const;
	void apply(const CloudTheme &data);

private:
	void parseThemes(const QVector<MTPTheme> &list);

	const not_null<Main::Session*> _session;
	int32 _hash = 0;
	mtpRequestId _requestId = 0;
	std::vector<CloudTheme> _list;
	rpl::event_stream<> _updates;

};

} // namespace Data
