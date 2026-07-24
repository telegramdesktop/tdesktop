/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/premium_3d_cover.h"

namespace Ui::Premium {

class DiamondRenderer;

class Diamond final : public Object3dCover {
public:
	explicit Diamond(QWidget *parent);
	~Diamond();

protected:
	std::unique_ptr<Ui::GL::Renderer> createRenderer() override;
	void applyState(
		float yaw,
		float pitch,
		float time,
		float alpha,
		bool night) override;

private:
	DiamondRenderer *_renderer = nullptr;

};

} // namespace Ui::Premium
