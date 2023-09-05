/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "platform/mac/integration_mac.h"

#include "platform/platform_integration.h"

namespace Platform {
namespace {

class MacIntegration final : public Integration {
};

} // namespace

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<MacIntegration>();
}

} // namespace Platform
