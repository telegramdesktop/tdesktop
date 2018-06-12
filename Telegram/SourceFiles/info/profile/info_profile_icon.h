/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Info {
namespace Profile {

class FloatingIcon : public Ui::RpWidget {
public:
	FloatingIcon(
		RpWidget *parent,
		const style::icon &icon,
		QPoint position);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Tag {
	};
	FloatingIcon(
		RpWidget *parent,
		const style::icon &icon,
		QPoint position,
		const Tag &);

	not_null<const style::icon*> _icon;
	QPoint _point;

};

} // namespace Profile
} // namespace Info
