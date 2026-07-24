/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/translate_provider_mac.h"

#ifndef TDESKTOP_DISABLE_SWIFT6

#include "base/weak_ptr.h"
#include "spellcheck/platform/platform_language.h"
#include "translate_provider_mac_swift_bridge.h"

namespace Platform {
namespace {

[[nodiscard]] Ui::TranslateProviderError ParseErrorCode(
		const char *errorUtf8) {
	return !std::strcmp(errorUtf8, "local-language-pack-missing")
		? Ui::TranslateProviderError::LocalLanguagePackMissing
		: Ui::TranslateProviderError::Unknown;
}

class TranslateProvider final : public Ui::TranslateProvider
, public base::has_weak_ptr {
public:
	[[nodiscard]] bool supportsMessageId() const override {
		return false;
	}

	void request(
			Ui::TranslateProviderRequest request,
			LanguageId to,
			Fn<void(Ui::TranslateProviderResult)> done) override {
		if (request.text.text.isEmpty()) {
			done(Ui::TranslateProviderResult{
				.error = Ui::TranslateProviderError::Unknown,
			});
			return;
		}
		const auto text = request.text.text.toUtf8();
		const auto target = to.twoLetterCode().toUtf8();
		if (target.isEmpty()) {
			done(Ui::TranslateProviderResult{
				.error = Ui::TranslateProviderError::Unknown,
			});
			return;
		}
		struct CallbackContext {
			base::weak_ptr<TranslateProvider> provider;
			Fn<void(Ui::TranslateProviderResult)> done;
		};
		auto ownedContext = std::make_unique<CallbackContext>(CallbackContext{
			.provider = base::make_weak(this),
			.done = std::move(done),
		});
		TranslateProviderMacSwiftTranslate(
			text.constData(),
			target.constData(),
			ownedContext.release(),
			[](void *context, const char *resultUtf8, const char *errorUtf8) {
				auto guard = std::unique_ptr<CallbackContext>(
					static_cast<CallbackContext*>(context));
				auto done = std::move(guard->done);
				const auto isAlive = (guard->provider.get() != nullptr);
				auto result = Ui::TranslateProviderResult();
				if (resultUtf8 != nullptr) {
					result.text = TextWithEntities{
						.text = QString::fromUtf8(resultUtf8),
					};
					std::free(const_cast<char*>(resultUtf8));
				}
				if (errorUtf8 != nullptr) {
					result.error = ParseErrorCode(errorUtf8);
					std::free(const_cast<char*>(errorUtf8));
				} else if (!result.text.has_value()) {
					result.error = Ui::TranslateProviderError::Unknown;
				}
				if (!isAlive) {
					return;
				}
				crl::on_main([=,
						done = std::move(done),
						result = std::move(result)] {
					done(std::move(result));
				});
			});
	}

};

} // namespace

std::unique_ptr<Ui::TranslateProvider> CreateTranslateProvider() {
	if (TranslateProviderMacSwiftIsAvailable()) {
		return std::make_unique<TranslateProvider>();
	}
	return nullptr;
}

bool IsTranslateProviderAvailable() {
	return TranslateProviderMacSwiftIsAvailable();
}

#else // TDESKTOP_DISABLE_SWIFT6

// Local on-device translation disabled (no Swift 6).
namespace Platform {

std::unique_ptr<Ui::TranslateProvider> CreateTranslateProvider() {
	return nullptr;
}

bool IsTranslateProviderAvailable() {
	return false;
}

#endif // TDESKTOP_DISABLE_SWIFT6

} // namespace Platform
