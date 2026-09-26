/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui::Premium {

enum class PromoParticles : uchar {
	Stars,
	Matrix,
	SpeedLines,
	Hello,
	ChatManagement,
	Emoji,
	Ads,
	Userpics,
	Tags,
	Formatting,
	ProfileBadge,
};

class PromoParticlesPainter {
public:
	virtual ~PromoParticlesPainter() = default;

	virtual void setGeometry(QRect outer, QRect device) = 0;

	virtual void setVideoProgress(float64 progress) {
	}

	virtual void paint(QPainter &p) = 0;
};

[[nodiscard]] std::unique_ptr<PromoParticlesPainter> MakePromoParticles(
	PromoParticles type);

} // namespace Ui::Premium
