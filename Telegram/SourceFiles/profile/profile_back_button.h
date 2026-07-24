/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/text/text.h"

class QGraphicsOpacityEffect;

namespace Profile {

class BackButton final : public Ui::AbstractButton {
public:
	BackButton(QWidget *parent);

	void setText(const QString &text);
	void setSubtext(const QString &subtext);
	void setWidget(not_null<Ui::RpWidget*> widget);
	void setOpacity(float64 opacity);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	void updateCache();

	rpl::lifetime _unreadBadgeLifetime;
	Ui::Text::String _text;
	Ui::Text::String _subtext;
	Ui::RpWidget *_widget = nullptr;

	int _cachedWidth = -1;
	int _elisionWidth = 0;

	float64 _opacity = 1.0;
	QGraphicsOpacityEffect *_opacityEffect = nullptr;

};

} // namespace Profile
