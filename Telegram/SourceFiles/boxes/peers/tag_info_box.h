/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
class Show;
} // namespace Ui

namespace HistoryView {
enum class BadgeRole : uchar;
} // namespace HistoryView

class PeerData;

void TagInfoBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	not_null<PeerData*> author,
	const QString &tagText,
	HistoryView::BadgeRole role);
