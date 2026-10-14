/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "wallet/wallet_amount_painter.h"

class UserData;

namespace Info::Profile {

class BalancePill final : public Ui::RpWidget {
public:
	BalancePill(
		QWidget *parent,
		not_null<UserData*> user,
		rpl::producer<std::optional<QColor>> edgeColor);

	void place(int outerWidth, int statusTop, float64 progress);

protected:
	void paintEvent(QPaintEvent *e) override;

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

private:
	void setContent(const QString &label, int64 balance);
	void refreshIcon();
	void refreshGeometry();

	Wallet::AmountPainter _amount;
	std::optional<QColor> _edgeColor;
	QString _label;
	QString _labelShown;
	QImage _icon;
	float64 _iconTop = 0.;
	float64 _opacity = 0.;
	int _labelSpace = 0;
	int _baseline = 0;
	int _arrowTop = 0;
	int _outerWidth = 0;
	int _top = 0;

};

} // namespace Info::Profile
