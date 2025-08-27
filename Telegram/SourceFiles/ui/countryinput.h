/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace style {
struct InputField;
} // namespace style

namespace Countries {
struct Info;
} // namespace Countries

namespace Ui {
class Show;
} // namespace Ui

class CountryInput : public Ui::RpWidget {
public:
	CountryInput(
		QWidget *parent,
		std::shared_ptr<Ui::Show> show,
		const style::InputField &st);

	[[nodiscard]] QString iso() const {
		return _chosenIso;
	}
	bool chooseCountry(const QString &country);

	void onChooseCode(const QString &code);

	rpl::producer<QString> codeChanged() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void chooseCountry(not_null<const Countries::Info*> info, int codeIndex);
	void setText(const QString &newText);

	const std::shared_ptr<Ui::Show> _show;
	const style::InputField &_st;
	bool _active = false;
	QString _text;
	QString _chosenIso;

	rpl::event_stream<QString> _codeChanged;

};
