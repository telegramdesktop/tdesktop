/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/profile/info_profile_music_button.h"
#include "ui/abstract_button.h"
#include "wallet/wallet_amount_painter.h"

class UserData;

namespace Info::Profile {

class BalancePill final : public Ui::AbstractButton {
public:
	BalancePill(
		QWidget *parent,
		not_null<UserData*> user,
		rpl::producer<std::optional<QColor>> edgeColor);

	void setOpacity(float64 opacity);
	[[nodiscard]] int fittedWidth() const;
	[[nodiscard]] bool actionable() const;
	[[nodiscard]] uint64 appearanceRevision() const;
	[[nodiscard]] QImage grabEmpty();
	void paintContentAlone(QPainter &p) const;

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

	QString accessibilityName() override;

private:
	void setContent(const QString &label, int64 balance);
	void refreshColors();
	void refreshLayout(int newWidth);
	void paintContent(QPainter &p) const;

	Wallet::AmountPainter _amount;
	std::optional<QColor> _edgeColor;
	PillColors _colors;
	QString _label;
	QString _labelShown;
	QImage _icon;
	float64 _iconTop = 0.;
	float64 _opacity = 0.;
	int _labelSpace = 0;
	int _fittedWidth = 0;
	int _baseline = 0;
	int _arrowTop = 0;
	uint64 _appearanceRevision = 0;
	bool _contentHidden = false;

};

} // namespace Info::Profile
