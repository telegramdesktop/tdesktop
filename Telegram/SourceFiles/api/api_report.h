/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;
class PhotoData;

namespace Ui {
enum class ReportReason;
} // namespace Ui

namespace Api {

void SendReport(
	not_null<QWidget*> toastParent,
	not_null<PeerData*> peer,
	Ui::ReportReason reason,
	const QString &comment,
	std::variant<v::null_t, MessageIdsList, not_null<PhotoData*>> data);

} // namespace Api
