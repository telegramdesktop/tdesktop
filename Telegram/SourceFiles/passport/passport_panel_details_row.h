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

namespace Passport {

class PanelDetailsRow : public Ui::RpWidget {
public:
	PanelDetailsRow(
		QWidget *parent,
		const QString &label,
		const QString &value);

	QPointer<Ui::InputField> field() const;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	QString _label;
	object_ptr<Ui::InputField> _field;

};

} // namespace Passport
