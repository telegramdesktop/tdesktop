/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

namespace Data {
struct CountryInfo;
} // namespace Data

namespace Ui {
class MultiSelect;
class RippleAnimation;
} // namespace Ui

class CountryInput : public Ui::RpWidget {
	Q_OBJECT

public:
	CountryInput(QWidget *parent, const style::InputField &st);

	[[nodiscard]] QString iso() const {
		return _chosenIso;
	}
	bool chooseCountry(const QString &country);

public Q_SLOTS:
	void onChooseCode(const QString &code);

Q_SIGNALS:
	void codeChanged(const QString &code);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void setText(const QString &newText);

	const style::InputField &_st;
	bool _active = false;
	QString _text;
	QString _chosenIso;
	QPainterPath _placeholderPath;

};
