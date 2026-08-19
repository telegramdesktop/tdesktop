/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/premium_3d_cover.h"

namespace Ui::Premium {

class CoinRenderer;

class Coin final : public Object3dCover {
public:
	explicit Coin(QWidget *parent);
	~Coin();

protected:
	std::unique_ptr<Ui::GL::Renderer> createRenderer() override;
	void applyState(
		float yaw,
		float pitch,
		float time,
		float alpha,
		bool night) override;

private:
	CoinRenderer *_renderer = nullptr;

};

} // namespace Ui::Premium
