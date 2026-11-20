/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/src/backend.h"
#include "tgw/tgw_widget_meter.h"

#include <QtGui/QFontMetricsF>

namespace twidget {
class SandboxHostedWait;
} // namespace twidget

namespace Tgw {

class Canvases;
class EmojiAccount;
struct FontPosition;

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

	QtBackend(
		Diagnostic diagnostic,
		Fn<void()> refused,
		QString regularFamily,
		QString monospaceFamily,
		std::vector<FontPosition> positions);
	~QtBackend();

	int create_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		const std::string &features,
		twlite::FontMetrics &out) override;
	int create_canvas_font(
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
	[[nodiscard]] const EmojiAccount &emoji() const;
	bool beginFrame();
	void noteCanvasFontRefused();
	void releaseLive();

	[[nodiscard]] WidgetMeter::Operation operation();
	[[nodiscard]] WidgetMeter::Work work();
	[[nodiscard]] WidgetMeter::Work workUnlessHeld();
	[[nodiscard]] crl::time heldFor() const;
	void input();
	void publishWaits(std::shared_ptr<twidget::SandboxHostedWait> wait);
	[[nodiscard]] bool refused() const;
	[[nodiscard]] const QString &refusal() const;

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

	[[nodiscard]] bool serves(const WidgetMeter::Work &work) const;
	[[nodiscard]] int makeFont(
		FontKey key,
		twlite::FontMetrics &out,
		bool canvas);
	[[nodiscard]] bool admitFamilies(const std::string &families);
	[[nodiscard]] int admitCanvasFont(FontKey key, twlite::FontMetrics &out);
	[[nodiscard]] int refuseCanvasFont(twlite::FontMetrics &out);
	void refuse(QString reason);

	const Diagnostic _diagnostic;
	const Fn<void()> _refused;
	const QString _regularFamily;
	const QString _monospaceFamily;
	const std::vector<FontPosition> _positions;
	base::flat_map<int, QtFont> _fonts;
	base::flat_map<int, Image> _images;
	base::flat_map<FontKey, CachedFont> _canvasFonts;
	const std::unique_ptr<Canvases> _canvases;
	const std::unique_ptr<EmojiAccount> _emoji;
	WidgetMeter _meter;
	QString _refusal;
	crl::time _framed = 0;
	int64 _imageBytes = 0;
	int _lastFontId = 0;
	int _canvasFontsLeft = 0;
	bool _canvasFontLimitNoted = false;
	bool _released = false;

};

} // namespace Tgw
