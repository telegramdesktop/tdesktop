/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "renderer-litehtml/src/backend.h"

#include <QtGui/QFontMetricsF>

namespace Tgw {

struct QtFont {
	QFont font;
	QFontMetricsF metrics;
	float64 scale = 1.;
};

class QtBackend final : public twlite::Backend {
public:
	explicit QtBackend(float64 ratio);
	~QtBackend();

	int create_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		twlite::FontMetrics &out) override;
	void delete_font(int font) override;
	float text_width(int font, const std::string &text) override;
	bool load_image(
		int id,
		const std::string &url,
		float &width,
		float &height) override;

	[[nodiscard]] const QtFont *font(int id) const;
	[[nodiscard]] const QImage *image(int id) const;

private:
	const float64 _ratio = 1.;
	base::flat_map<int, QtFont> _fonts;
	base::flat_map<int, QImage> _images;
	int64 _imageBytes = 0;
	int _lastFontId = 0;

};

} // namespace Tgw
