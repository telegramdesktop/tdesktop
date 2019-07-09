/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
class ToggleView;
} // namespace Ui

namespace Info {
namespace Profile {

class Button : public Ui::RippleButton {
public:
	Button(
		QWidget *parent,
		rpl::producer<QString> &&text);
	Button(
		QWidget *parent,
		rpl::producer<QString> &&text,
		const style::InfoProfileButton &st);

	Button *toggleOn(rpl::producer<bool> &&toggled);
	bool toggled() const;
	rpl::producer<bool> toggledChanges() const;
	rpl::producer<bool> toggledValue() const;

	void setColorOverride(std::optional<QColor> textColorOverride);

protected:
	int resizeGetHeight(int newWidth) override;
	void onStateChanged(
		State was,
		StateChangeSource source) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setText(QString &&text);
	QRect toggleRect() const;
	void updateVisibleText(int newWidth);

	const style::InfoProfileButton &_st;
	QString _original;
	QString _text;
	int _originalWidth = 0;
	int _textWidth = 0;
	std::unique_ptr<Ui::ToggleView> _toggle;
	std::optional<QColor> _textColorOverride;

};

} // namespace Profile
} // namespace Info
