/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class InputField;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

class PeerData;
class UserData;

namespace HistoryView {
enum class BadgeRole : uchar;
} // namespace HistoryView

[[nodiscard]] HistoryView::BadgeRole LookupBadgeRole(
	not_null<PeerData*> peer,
	not_null<UserData*> user);

class EditTagControl final : public Ui::RpWidget {
public:
	EditTagControl(
		QWidget *parent,
		not_null<Main::Session*> session,
		not_null<UserData*> user,
		const QString &currentRank,
		HistoryView::BadgeRole role);
	~EditTagControl();

	[[nodiscard]] QString currentRank() const;
	[[nodiscard]] not_null<Ui::InputField*> field() const;

private:
	class PreviewWidget;

	not_null<PreviewWidget*> _preview;
	not_null<Ui::InputField*> _field;

};
