/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QColor;

class QPointF;

namespace Main {
class Session;
} // namespace Main

namespace Editor {

struct WeatherInfo {
	QString emoji;
	float64 celsius = 0.;
};

[[nodiscard]] std::optional<WeatherInfo> CachedWeather();

[[nodiscard]] bool WeatherInCelsius();

[[nodiscard]] QString WeatherTemperatureText(float64 celsius);

[[nodiscard]] QColor WeatherTextColor(QColor background);

void FetchWeather(
	not_null<Main::Session*> session,
	QPointF point,
	Fn<void(WeatherInfo)> done,
	Fn<void()> fail);

void FetchWeatherForCurrentLocation(
	not_null<Main::Session*> session,
	Fn<void(WeatherInfo)> done,
	Fn<void()> fail);

} // namespace Editor
