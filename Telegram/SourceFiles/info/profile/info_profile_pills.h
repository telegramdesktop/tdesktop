/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Info::Profile {

class BalancePill;
class MusicButton;

class PillsRow final {
public:
	PillsRow(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		rpl::producer<std::optional<QColor>> edgeColor,
		Fn<void()> showSavedMusic,
		bool underButtons);
	~PillsRow();

	[[nodiscard]] rpl::producer<int> reserveValue() const;
	void place(int top, float64 progress);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void layout();

	const not_null<Ui::RpWidget*> _parent;
	const std::unique_ptr<BalancePill> _balance;
	const std::unique_ptr<MusicButton> _music;
	rpl::variable<int> _reserve = 0;
	int _outerWidth = 0;
	int _top = 0;
	rpl::lifetime _lifetime;

};

} // namespace Info::Profile
