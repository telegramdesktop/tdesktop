/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/glass/glass_policy.h"

#include "platform/platform_glass_capabilities.h"
#include "settings.h"
#include "ui/effects/animations.h"
#include "ui/glass/glass_option.h"
#include "ui/power_saving.h"

namespace Glass {
namespace {

[[nodiscard]] bool Killed() {
	return cNoLiquidGlass();
}

} // namespace

bool Supported() {
#ifdef Q_OS_WIN
	return true;
#else
	return false;
#endif
}

Policy Current() {
	const auto capabilities = Platform::QueryGlassCapabilities();
	// The kill switch outranks the preference, so support can always stop it.
	const auto quality = (Supported()
		&& capabilities.composition
		&& !Killed()
		&& Enabled())
		? Quality::Solid
		: Quality::Off;
	auto weight = Weight::Tinted;
	if (capabilities.highContrast) {
		weight = Weight::HighContrast;
	} else if (PowerSaving::ForceAll()
		|| capabilities.remoteSession
		|| !capabilities.transparency) {
		weight = Weight::Opaque;
	}
	return {
		.quality = quality,
		.weight = weight,
		.motionReduced = anim::Disabled()
			|| PowerSaving::On(PowerSaving::kAnimations)
			|| capabilities.reducedMotion,
	};
}

rpl::producer<> Changes() {
	return PowerSaving::Changes();
}

} // namespace Glass
