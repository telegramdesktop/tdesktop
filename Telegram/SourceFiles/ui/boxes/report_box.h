/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;

enum class ReportSource {
	Message,
	Channel,
	Group,
	Bot,
};

enum class ReportReason {
	Spam,
	Fake,
	Violence,
	ChildAbuse,
	Pornography,
	Other,
};

void ReportReasonBox(
	not_null<GenericBox*> box,
	ReportSource source,
	Fn<void(ReportReason)> done);

void ReportDetailsBox(
	not_null<GenericBox*> box,
	Fn<void(QString)> done);

} // namespace Ui
