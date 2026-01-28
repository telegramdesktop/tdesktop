/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"

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
	QString _text;
	QString _subtext;
	Ui::RpWidget *_widget = nullptr;

	int _cachedWidth = -1;
	QString _cachedElidedText;
	QString _cachedElidedSubtext;

	float64 _opacity = 1.0;
	QGraphicsOpacityEffect *_opacityEffect = nullptr;

};

} // namespace Profile
