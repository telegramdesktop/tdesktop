/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Passport {

class FormRow : public Ui::RippleButton {
public:
	FormRow(
		QWidget *parent,
		const QString &title,
		const QString &description);

	void setReady(bool ready);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	int countAvailableWidth() const;
	int countAvailableWidth(int newWidth) const;

	Text _title;
	Text _description;
	int _titleHeight = 0;
	int _descriptionHeight = 0;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _checkbox = { nullptr };

};

} // namespace Passport
