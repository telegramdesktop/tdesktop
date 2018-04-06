/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
} // namespace Ui

namespace Passport {

class PanelLabel : public Ui::PaddingWrap<Ui::FlatLabel> {
public:
	using PaddingWrap::PaddingWrap;

	int naturalWidth() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<BoxContentDivider> _background = object_ptr<BoxContentDivider>(this);

};

class PanelDetailsRow : public Ui::RpWidget {
public:
	PanelDetailsRow(
		QWidget *parent,
		const QString &label,
		const QString &value);

	bool setFocusFast();
	QString getValue() const;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	QString _label;
	object_ptr<Ui::InputField> _field;

};

} // namespace Passport
