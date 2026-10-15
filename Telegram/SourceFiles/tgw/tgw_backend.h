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

class Canvases;

struct QtFont {
	QFont font;
	QFontMetricsF metrics;
	float64 scale = 1.;
};

class QtBackend final : public twlite::Backend {
public:
	using Diagnostic = Fn<void(
		const std::string &kind,
		const std::string &detail)>;

	QtBackend(float64 ratio, Diagnostic diagnostic);
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
	void canvas_run(
		int id,
		int width,
		int height,
		const std::vector<twlite::CanvasOp> &ops) override;
	bool canvas_read(
		int id,
		int x,
		int y,
		int width,
		int height,
		std::vector<uint8_t> &rgba) override;
	void canvas_release(int id) override;

	[[nodiscard]] const QtFont *font(int id) const;
	[[nodiscard]] const QImage *image(int id) const;
	[[nodiscard]] QSizeF imageSize(int id) const;
	[[nodiscard]] const QImage *canvas(int id) const;
	void beginCanvasBatch();

private:
	struct Image {
		QImage image;
		QSizeF size;
	};

	const float64 _ratio = 1.;
	base::flat_map<int, QtFont> _fonts;
	base::flat_map<int, Image> _images;
	const std::unique_ptr<Canvases> _canvases;
	int64 _imageBytes = 0;
	int _lastFontId = 0;

};

} // namespace Tgw
