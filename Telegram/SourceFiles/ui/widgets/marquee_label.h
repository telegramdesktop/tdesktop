/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"

class Painter;

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {

class PopupMenu;

class MarqueeLabel final : public RpWidget {
public:
	MarqueeLabel(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::FlatLabel &st);

	[[nodiscard]] const style::FlatLabel &st() const {
		return _st;
	}

	void setTextColorOverride(std::optional<QColor> color);
	void setContextCopyText(const QString &copyText);

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void setText(const QString &text);
	void refreshMarqueeState();
	bool marqueeStep(crl::time now);
	void paintMarquee(Painter &p);

	const style::FlatLabel &_st;
	Text::String _text;
	std::optional<QColor> _textColorOverride;
	QString _contextCopyText;
	base::unique_qptr<PopupMenu> _menu;

	QImage _frame;
	Animations::Basic _marquee;
	crl::time _lastUpdate = 0;
	crl::time _delay = 0;
	float64 _offset = 0.;
	int _availableTextWidth = 0;
	bool _overflown = false;

};

} // namespace Ui
