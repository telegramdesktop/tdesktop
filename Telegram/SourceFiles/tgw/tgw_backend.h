/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/src/backend.h"

#include <QtGui/QFontMetricsF>

namespace Tgw {

class Canvases;

struct QtFont {
	QFont font;
	QFontMetricsF metrics;
	float64 scale = 1.;
	bool canvas = false;
};

class QtBackend final : public twlite::Backend {
public:
	using Diagnostic = Fn<void(
		const std::string &kind,
		const std::string &detail)>;

	explicit QtBackend(Diagnostic diagnostic);
	~QtBackend();

	int create_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		const std::string &features,
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
	[[nodiscard]] int canvasFont(
		const std::string &css,
		twlite::FontMetrics &metrics);
	void releaseCanvasFonts();

private:
	using FontKey = std::pair<uint32, float64>;

	struct Image {
		QImage image;
		QSizeF size;
	};

	struct CachedFont {
		int id = 0;
		twlite::FontMetrics metrics;
	};

	[[nodiscard]] int makeFont(
		FontKey key,
		twlite::FontMetrics &out,
		bool canvas);
	[[nodiscard]] int admitCanvasFont(FontKey key, twlite::FontMetrics &out);
	[[nodiscard]] int refuseCanvasFont(twlite::FontMetrics &out);

	const Diagnostic _diagnostic;
	base::flat_map<int, QtFont> _fonts;
	base::flat_map<int, Image> _images;
	base::flat_map<FontKey, CachedFont> _canvasFonts;
	base::flat_map<std::string, FontKey> _canvasSpellings;
	const std::unique_ptr<Canvases> _canvases;
	int64 _imageBytes = 0;
	int _lastFontId = 0;
	int _canvasFontsLeft = 0;
	int _canvasParsesLeft = 0;
	bool _canvasFontLimitNoted = false;

};

} // namespace Tgw
