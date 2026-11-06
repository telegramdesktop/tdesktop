/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;

namespace Ui {
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

class AddressChips final {
public:
	[[nodiscard]] static std::optional<AddressChips> FromAddress(
		const QString &address);

	void paintPlates(QPainter &p, QRect line) const;
	void paintTexts(QPainter &p, QRect line) const;
	void paint(QPainter &p, QRect line) const;

private:
	explicit AddressChips(std::array<QString, 3> texts);

	std::array<QString, 3> _texts;

};

[[nodiscard]] not_null<Ui::RpWidget*> AddHistoryRowTitle(
	not_null<Ui::VerticalLayout*> container,
	const QString &title,
	const QString &address,
	int lineHeight,
	style::margins margins,
	not_null<QWidget*> button,
	Fn<void(Fn<void(Painter&)>)> paintUnderRipple);

void PaintSendingRowTitle(
	QPainter &p,
	QRect line,
	const std::optional<AddressChips> &now,
	const std::optional<AddressChips> *next,
	bool same,
	float64 progress,
	Fn<void(bool next, float64 opacity)> text);

} // namespace Wallet
