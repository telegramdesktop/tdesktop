/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;
class PhotoData;

namespace Ui {
class Show;
enum class ReportReason;
} // namespace Ui

namespace Data {
struct ReportInput;
} // namespace Data

namespace Api {

struct ReportResult final {
	using Id = QByteArray;
	struct Option final {
		Id id = 0;
		QString text;
	};
	using Options = std::vector<Option>;
	Options options;
	QString title;
	QString error;
	QString comment;
	struct CommentOption {
		bool optional = false;
		Id id = 0;
	};
	std::optional<CommentOption> commentOption;
	bool successful = false;
};

void SendReport(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	Ui::ReportReason reason,
	const QString &comment,
	std::variant<v::null_t, not_null<PhotoData*>> data);

[[nodiscard]] auto CreateReportMessagesOrStoriesCallback(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer)
-> Fn<void(Data::ReportInput, Fn<void(ReportResult)>)>;

} // namespace Api
