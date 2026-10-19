/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_diamond.h"

#include "ui/effects/premium_diamond_renderer.h"

namespace Ui::Premium {
namespace {

constexpr auto kSpinDuration = crl::time(12000);
constexpr auto kRespinDelay = crl::time(0);
constexpr auto kSparklePeriod = 9.;
constexpr auto kSampleCount = 4; // MSAA — smooths the faceted silhouette.

} // namespace

Diamond::Diamond(QWidget *parent)
: Object3dCover(parent, {
	.spinDuration = kSpinDuration,
	.respinDelay = kRespinDelay,
	.period = kSparklePeriod,
	.spinEaseOut = false,
	.sampleCount = kSampleCount,
}) {
}

Diamond::~Diamond() = default;

std::unique_ptr<Ui::GL::Renderer> Diamond::createRenderer() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API
	auto renderer = std::make_unique<DiamondRenderer>();
	_renderer = renderer.get();
	return renderer;
#else // Qt >= 6.7 && !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return nullptr;
#endif // Qt < 6.7 || DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

void Diamond::applyState(
		float yaw,
		float pitch,
		float time,
		float alpha,
		bool night) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API
	if (_renderer) {
		_renderer->setState({
			.yaw = yaw,
			.pitch = pitch,
			.time = time,
			.alpha = alpha,
			.night = night,
		});
	}
#endif // Qt >= 6.7 && !DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

} // namespace Ui::Premium
