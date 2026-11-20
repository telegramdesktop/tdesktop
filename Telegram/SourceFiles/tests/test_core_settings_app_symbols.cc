/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/application.h"
#include "data/data_wall_paper.h"
#include "lang/lang_keys.h"
#include "lang/lang_values.h"
#include "platform/platform_notifications_manager.h"
#include "window/themes/window_theme.h"

#include <crl/crl_on_main.h>

// WHY: the linked settings code references these app entries; tests never reach them.
namespace Core {

Application &App() {
	Unexpected("Core::App in test_core_settings.");
}

Settings &Application::settings() {
	Unexpected("Core::Application::settings in test_core_settings.");
}

} // namespace Core

namespace Data {

const std::vector<QColor> WallPaper::backgroundColors() const {
	Unexpected("Data::WallPaper::backgroundColors in test_core_settings.");
}

float64 WallPaper::patternOpacity() const {
	Unexpected("Data::WallPaper::patternOpacity in test_core_settings.");
}

int WallPaper::gradientRotation() const {
	Unexpected("Data::WallPaper::gradientRotation in test_core_settings.");
}

WallPaper DefaultWallPaper() {
	Unexpected("Data::DefaultWallPaper in test_core_settings.");
}

} // namespace Data

namespace Lang {

QString Id() {
	Unexpected("Lang::Id in test_core_settings.");
}

} // namespace Lang

namespace Lang::details {

rpl::producer<QString> Value(ushort key) {
	Unexpected("Lang::details::Value in test_core_settings.");
}

} // namespace Lang::details

namespace Platform::Notifications {

bool ByDefault() {
	Unexpected("Platform::Notifications::ByDefault in test_core_settings.");
}

} // namespace Platform::Notifications

namespace Window::Theme {

bool IsEmbeddedTheme(const QString &path) {
	Unexpected("Window::Theme::IsEmbeddedTheme in test_core_settings.");
}

bool LoadFromFile(
		const QString &path,
		not_null<Instance*> out,
		Cached *outCache,
		QByteArray *outContent,
		const style::colorizer &colorizer) {
	Unexpected("Window::Theme::LoadFromFile in test_core_settings.");
}

std::optional<QByteArray> ReadPaletteContent(const QString &path) {
	Unexpected("Window::Theme::ReadPaletteContent in test_core_settings.");
}

bool ReadPaletteValues(
		const QByteArray &content,
		Fn<bool(QLatin1String name, QLatin1String value)> callback) {
	Unexpected("Window::Theme::ReadPaletteValues in test_core_settings.");
}

} // namespace Window::Theme

namespace crl {

rpl::producer<> on_main_update_requests() {
	Unexpected("crl::on_main_update_requests in test_core_settings.");
}

} // namespace crl
