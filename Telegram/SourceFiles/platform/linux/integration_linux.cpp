/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/integration_linux.h"

#include "platform/platform_integration.h"

namespace Platform {
namespace {

class LinuxIntegration final : public Integration {
};

} // namespace

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<LinuxIntegration>();
}

} // namespace Platform
