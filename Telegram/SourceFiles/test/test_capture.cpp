/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_capture.h"

#include "test/test_log.h"
#include "ui/layers/box_layer_widget.h"
#include "ui/ui_utility.h"

#include "styles/palette.h"

#include <QtGui/QPainter>

namespace Test {
namespace {

constexpr auto kBlankSpreadThreshold = 6;
constexpr auto kContactSheetGap = 8;
constexpr auto kCoverageSamples = 32;
constexpr auto kUnpaintedMinPermille = 100;
constexpr auto kBackgroundOwnerHops = 6;
constexpr auto kWalkedChainHead = 3;

[[nodiscard]] QString WithPngExtension(const QString &name) {
	return name.endsWith(u".png"_q, Qt::CaseInsensitive)
		? name
		: (name + u".png"_q);
}

[[nodiscard]] QString RectText(const QRect &rect) {
	return u"%1,%2 %3x%4"_q
		.arg(rect.x())
		.arg(rect.y())
		.arg(rect.width())
		.arg(rect.height());
}

[[nodiscard]] QString MisframedDetails(
		not_null<QWidget*> widget,
		const QRect &logicalRect) {
	const auto bounds = widget->rect();
	if (!logicalRect.isEmpty() && bounds.contains(logicalRect)) {
		return QString();
	}
	const auto inside = bounds.intersected(logicalRect);
	return u"requested rect is not fully inside the grabbed widget: "
		u"requested=%1 widget=%2 inside=%3 rows=%4/%5 columns=%6/%7"_q
		.arg(RectText(logicalRect), RectText(bounds), RectText(inside))
		.arg(inside.height())
		.arg(logicalRect.height())
		.arg(inside.width())
		.arg(logicalRect.width());
}

[[nodiscard]] std::vector<QPoint> SamplePoints(const QSize &size) {
	auto result = std::vector<QPoint>();
	const auto columns = std::min(size.width(), kCoverageSamples);
	const auto rows = std::min(size.height(), kCoverageSamples);
	if (columns < 1 || rows < 1) {
		return result;
	}
	result.reserve(columns * rows);
	for (auto y = 0; y != rows; ++y) {
		for (auto x = 0; x != columns; ++x) {
			result.push_back(QPoint(
				((2 * x + 1) * size.width()) / (2 * columns),
				((2 * y + 1) * size.height()) / (2 * rows)));
		}
	}
	return result;
}

[[nodiscard]] int SampledMatchPermille(
		const QImage &image,
		const QColor &color) {
	const auto points = SamplePoints(image.size());
	if (points.empty()) {
		return 0;
	}
	const auto reference = color.rgb();
	auto matched = 0;
	for (const auto &point : points) {
		if (image.pixelColor(point).rgb() == reference) {
			++matched;
		}
	}
	return (1000 * matched) / int(points.size());
}

[[nodiscard]] int UnpaintedPermille(
		not_null<QWidget*> widget,
		const QRect &logicalRect) {
	if (widget->testAttribute(Qt::WA_OpaquePaintEvent)) {
		return 0;
	}
	const auto rect = logicalRect.isEmpty()
		? widget->rect()
		: logicalRect.intersected(widget->rect());
	if (rect.isEmpty()) {
		return 0;
	}
	const auto firstSentinel = QColor(255, 0, 255);
	const auto secondSentinel = QColor(0, 255, 0);
	const auto first = Ui::GrabWidgetToImage(widget, rect, firstSentinel);
	const auto second = Ui::GrabWidgetToImage(widget, rect, secondSentinel);
	if (first.isNull() || first.size() != second.size()) {
		return 0;
	}
	const auto points = SamplePoints(first.size());
	if (points.empty()) {
		return 0;
	}
	const auto firstRgb = firstSentinel.rgb();
	const auto secondRgb = secondSentinel.rgb();
	auto unpainted = 0;
	for (const auto &point : points) {
		if (first.pixelColor(point).rgb() == firstRgb
			&& second.pixelColor(point).rgb() == secondRgb) {
			++unpainted;
		}
	}
	return (1000 * unpainted) / int(points.size());
}

[[nodiscard]] QString BackgroundOwnerDetails(
		not_null<QWidget*> widget,
		const QRect &logicalRect) {
	const auto rect = logicalRect.isEmpty() ? widget->rect() : logicalRect;
	const auto top = widget->window();
	auto ancestor = (widget.get() == top) ? nullptr : widget->parentWidget();
	for (auto hop = 0; ancestor && (hop != kBackgroundOwnerHops); ++hop) {
		const auto mapped = Ui::MapFrom(ancestor, widget, rect)
			.intersected(ancestor->rect());
		if (!mapped.isEmpty()) {
			const auto unpainted = UnpaintedPermille(ancestor, mapped);
			if (unpainted < kUnpaintedMinPermille) {
				return u"grab %1 instead (unpainted %2/1000)%3"_q
					.arg(WidgetDescription(ancestor))
					.arg(unpainted)
					.arg(dynamic_cast<Ui::BoxLayerWidget*>(ancestor)
						? u" - Test::PaintingLayerRoot() resolves it"_q
						: QString());
			}
		}
		ancestor = (ancestor == top) ? nullptr : ancestor->parentWidget();
	}
	return u"no ancestor within %1 parents paints a background"_q
		.arg(kBackgroundOwnerHops);
}

[[nodiscard]] QString BlankRootDetails(
		not_null<QWidget*> widget,
		const QImage &image,
		const QRect &logicalRect,
		bool logCoverage = true) {
	if (image.isNull()
		|| widget->testAttribute(Qt::WA_OpaquePaintEvent)
		|| widget->testAttribute(Qt::WA_NoSystemBackground)) {
		return QString();
	}
	const auto harnessThemeBase = st::windowBg->c;
	const auto baseMatched = SampledMatchPermille(image, harnessThemeBase);
	if (!baseMatched) {
		return QString();
	}
	const auto unpainted = UnpaintedPermille(widget, logicalRect);
	if (logCoverage) {
		Note(u"capture coverage: harnessThemeBase=%1/1000 unpainted=%2/1000 "
			u"(threshold %3/1000)"_q
			.arg(baseMatched)
			.arg(unpainted)
			.arg(kUnpaintedMinPermille));
	}
	if (unpainted < kUnpaintedMinPermille) {
		return QString();
	}
	return u"render root paints no background of its own: %1 has neither "
		u"Qt::WA_OpaquePaintEvent nor Qt::WA_NoSystemBackground, and %2/1000 "
		u"sampled points were painted by neither the widget nor its "
		u"children; the harness theme base is active st::windowBg %3 "
		u"(%4/1000 of the grab matches that base) "
		u"- %5"_q
		.arg(WidgetDescription(widget))
		.arg(unpainted)
		.arg(harnessThemeBase.name())
		.arg(baseMatched)
		.arg(BackgroundOwnerDetails(widget, logicalRect));
}

// The walk a refusal prints always names the window it stopped at, and never
// more than kWalkedChainHead entries before it, so a target buried deep in a
// widget tree cannot turn one refusal into a multi-kilobyte log line.
[[nodiscard]] QString ElidedChain(const QStringList &walked) {
	if (walked.size() <= kWalkedChainHead + 1) {
		return walked.join(u" < "_q);
	}
	auto shown = QStringList();
	for (auto i = 0; i != kWalkedChainHead; ++i) {
		shown.push_back(walked[i]);
	}
	shown.push_back(u"\u2026"_q);
	shown.push_back(walked.back());
	return shown.join(u" < "_q);
}

[[nodiscard]] QString ViaWindowText(const WindowMappedCapture &reading) {
	return u"window-mapped capture: target=%1 window=%2 mapped=%3 - a blank "
		u"frame is a Note and never a FAIL, because the decisive oracle for a "
		u"widget that paints no opaque background of its own is textual and "
		u"this capture only corroborates it%4"_q
		.arg(reading.identity.isEmpty() ? u"<none>"_q : reading.identity)
		.arg(reading.window
			? WidgetDescription(reading.window.data())
			: u"<none>"_q)
		.arg(RectText(reading.mapped))
		.arg(reading.refusal.isEmpty()
			? QString()
			: u" - %1"_q.arg(reading.refusal));
}

} // namespace

QString WidgetDescription(not_null<QWidget*> widget) {
	const auto &instance = *widget;
	return u"%1 %2"_q.arg(
		QString::fromUtf8(typeid(instance).name()),
		RectText(widget->geometry()));
}

QImage GrabWidget(not_null<QWidget*> widget) {
	return Ui::GrabWidgetToImage(widget, QRect(), st::windowBg->c);
}

QImage GrabRect(
		not_null<QWidget*> widget,
		const QRect &logicalRect) {
	const auto bounded = logicalRect.intersected(widget->rect());
	return bounded.isEmpty()
		? QImage()
		: Ui::GrabWidgetToImage(widget, bounded, st::windowBg->c);
}

bool LooksBlank(const QImage &image) {
	if (image.isNull() || image.width() < 2 || image.height() < 2) {
		return true;
	}
	auto minLuma = 255;
	auto maxLuma = 0;
	const auto columns = std::min(image.width(), 32);
	const auto rows = std::min(image.height(), 32);
	for (auto y = 0; y != rows; ++y) {
		for (auto x = 0; x != columns; ++x) {
			const auto pixel = image.pixelColor(
				(x * (image.width() - 1)) / std::max(columns - 1, 1),
				(y * (image.height() - 1)) / std::max(rows - 1, 1));
			const auto luma = int(std::round(255 * pixel.lightnessF()));
			minLuma = std::min(minLuma, luma);
			maxLuma = std::max(maxLuma, luma);
		}
	}
	return (maxLuma - minLuma) < kBlankSpreadThreshold;
}

bool PreparedWidgetCapture::prepare(QWidget *widget) {
	_widget = nullptr;
	_image = QImage();
	_globalGeometry = QRect();
	if (!widget) {
		_pendingReason = u"target does not exist"_q;
		return false;
	} else if (!widget->isVisible()) {
		_pendingReason = u"target is not visible: %1"_q.arg(
			WidgetDescription(widget));
		return false;
	} else if (widget->size().isEmpty()) {
		_pendingReason = u"target has empty geometry: %1"_q.arg(
			WidgetDescription(widget));
		return false;
	}
	const auto image = GrabWidget(widget);
	if (LooksBlank(image)) {
		_pendingReason = u"target grab still looks blank: %1"_q.arg(
			WidgetDescription(widget));
		return false;
	}
	const auto blankRoot = BlankRootDetails(widget, image, QRect(), false);
	if (!blankRoot.isEmpty()) {
		_pendingReason = blankRoot;
		return false;
	}
	_widget = widget;
	_image = image;
	_globalGeometry = QRect(widget->mapToGlobal(QPoint()), widget->size());
	_pendingReason = QString();
	return true;
}

void PreparedWidgetCapture::invalidate(QString reason) {
	_widget = nullptr;
	_image = QImage();
	_globalGeometry = QRect();
	_pendingReason = std::move(reason);
}

bool PreparedWidgetCapture::save(const QString &name) {
	if (!_widget || _image.isNull()) {
		Fail(
			u"prepared capture %1"_q.arg(name),
			_pendingReason.isEmpty()
				? u"no accepted frame"_q
				: _pendingReason);
		return false;
	}
	LogGeometry(name, _globalGeometry);
	const auto path = SaveImage(_image, name);
	if (path.isEmpty()) {
		Fail(u"prepared capture %1"_q.arg(name), u"could not save image"_q);
		return false;
	}
	return true;
}

QWidget *PreparedWidgetCapture::widget() const {
	return _widget.data();
}

const QImage &PreparedWidgetCapture::image() const {
	return _image;
}

QString PreparedWidgetCapture::pendingReason() const {
	return _pendingReason;
}

QString SaveImage(const QImage &image, const QString &name) {
	if (image.isNull()) {
		return QString();
	}
	const auto path = ScreenshotsDir() + WithPngExtension(name);
	if (!image.save(path, "PNG")) {
		return QString();
	}
	LogRaw(u"SCREENSHOT: %1"_q.arg(path));
	return path;
}

bool CaptureWidget(not_null<QWidget*> widget, const QString &name) {
	LogGeometry(name, QRect(widget->mapToGlobal(QPoint()), widget->size()));
	if (!widget->isVisible()) {
		Fail(u"capture %1"_q.arg(name), u"widget is not visible"_q);
		return false;
	}
	const auto image = GrabWidget(widget);
	if (LooksBlank(image)) {
		Fail(u"capture %1"_q.arg(name), u"grabbed image looks blank"_q);
		return false;
	}
	const auto blankRoot = BlankRootDetails(widget, image, QRect());
	if (!blankRoot.isEmpty()) {
		Fail(u"capture %1"_q.arg(name), blankRoot);
		return false;
	}
	return !SaveImage(image, name).isEmpty();
}

bool CaptureRect(
		not_null<QWidget*> widget,
		const QRect &logicalRect,
		const QString &name) {
	LogGeometry(name, logicalRect);
	if (!widget->isVisible()) {
		Fail(u"capture %1"_q.arg(name), u"widget is not visible"_q);
		return false;
	}
	const auto misframed = MisframedDetails(widget, logicalRect);
	if (!misframed.isEmpty()) {
		Fail(u"capture %1"_q.arg(name), misframed);
		return false;
	}
	const auto image = GrabRect(widget, logicalRect);
	if (LooksBlank(image)) {
		Fail(u"capture %1"_q.arg(name), u"grabbed image looks blank"_q);
		return false;
	}
	const auto blankRoot = BlankRootDetails(widget, image, logicalRect);
	if (!blankRoot.isEmpty()) {
		Fail(u"capture %1"_q.arg(name), blankRoot);
		return false;
	}
	return !SaveImage(image, name).isEmpty();
}

bool CaptureMappedRect(
		not_null<QWidget*> widget,
		not_null<QWidget*> rectOrigin,
		const QRect &logicalRect,
		const QString &name) {
	return CaptureRect(
		widget,
		Ui::MapFrom(widget, rectOrigin, logicalRect),
		name);
}

PaintingLayerRootResult PaintingLayerRoot(QWidget *box) {
	if (!box) {
		return {
			.refusal = u"no widget was handed to the painting layer root "
				u"resolver"_q,
		};
	}
	auto walked = QStringList();
	for (auto widget = box; widget; widget = widget->parentWidget()) {
		if (const auto layer = dynamic_cast<Ui::BoxLayerWidget*>(widget)) {
			return { .widget = layer };
		}
		walked.push_back(WidgetDescription(widget));
		if (widget == widget->window()) {
			break;
		}
	}
	return {
		.refusal = u"no Ui::BoxLayerWidget between the target and its own "
			u"window, so it is not a box inside a layer and has no painting "
			u"layer root; walked %1 widget(s) and stopped at the window: "
			u"[%2]"_q.arg(walked.size()).arg(ElidedChain(walked)),
	};
}

bool CaptureInLayerRoot(not_null<QWidget*> box, const QString &name) {
	const auto root = PaintingLayerRoot(box);
	if (!root.resolved()) {
		Fail(u"capture %1"_q.arg(name), root.refusal);
		return false;
	}
	return CaptureMappedRect(root.widget.data(), box, box->rect(), name);
}

WindowMappedCapture ReadViaWindow(QWidget *widget) {
	if (!widget) {
		return {
			.refusal = u"no widget was handed to the window-mapped "
				u"capture"_q,
		};
	}
	auto result = WindowMappedCapture{
		.identity = WidgetDescription(widget),
	};
	if (!widget->isVisible()) {
		result.refusal = u"target is not visible: %1"_q.arg(result.identity);
		return result;
	} else if (widget->size().isEmpty()) {
		result.refusal = u"target has empty geometry: %1"_q.arg(
			result.identity);
		return result;
	}
	const auto window = widget->window();
	if (window == widget) {
		result.refusal = u"target is its own window, so there is no opaque "
			u"window behind it to grab: %1"_q.arg(result.identity);
		return result;
	}
	result.mapped = Ui::MapFrom(window, widget, widget->rect());
	const auto misframed = MisframedDetails(window, result.mapped);
	if (!misframed.isEmpty()) {
		result.refusal = misframed;
		return result;
	}
	result.window = window;
	return result;
}

QImage GrabViaWindow(QWidget *widget) {
	const auto reading = ReadViaWindow(widget);
	return reading.resolved()
		? GrabRect(reading.window.data(), reading.mapped)
		: QImage();
}

bool ViaWindowReady(QWidget *widget) {
	return !LooksBlank(GrabViaWindow(widget));
}

QString ViaWindowDetails(QWidget *widget) {
	return ViaWindowText(ReadViaWindow(widget));
}

bool CaptureViaWindow(not_null<QWidget*> widget, const QString &name) {
	const auto reading = ReadViaWindow(widget);
	if (!reading.resolved()) {
		Fail(u"capture %1"_q.arg(name), reading.refusal);
		return false;
	}
	LogGeometry(name, QRect(widget->mapToGlobal(QPoint()), widget->size()));
	const auto image = GrabRect(reading.window.data(), reading.mapped);
	if (LooksBlank(image)) {
		Note(u"capture %1 declined a blank frame: %2"_q
			.arg(name, ViaWindowText(reading)));
		return false;
	}
	return !SaveImage(image, name).isEmpty();
}

QImage Crop(const QImage &image, const QRect &pixelRect) {
	const auto bounded = pixelRect.intersected(image.rect());
	return bounded.isEmpty() ? QImage() : image.copy(bounded);
}

QImage Zoom(const QImage &image, int factor) {
	return (image.isNull() || factor <= 1)
		? image
		: image.scaled(
			image.size() * factor,
			Qt::KeepAspectRatio,
			Qt::FastTransformation);
}

QImage ContactSheet(const std::vector<QImage> &images) {
	auto width = 0;
	auto height = 0;
	for (const auto &image : images) {
		if (image.isNull()) {
			continue;
		}
		width += image.width() + (width ? kContactSheetGap : 0);
		height = std::max(height, image.height());
	}
	if (!width) {
		return QImage();
	}
	auto result = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	auto painter = QPainter(&result);
	auto x = 0;
	for (const auto &image : images) {
		if (image.isNull()) {
			continue;
		}
		auto copy = image;
		copy.setDevicePixelRatio(1.);
		painter.drawImage(x, 0, copy);
		x += copy.width() + kContactSheetGap;
	}
	painter.end();
	return result;
}

} // namespace Test

#endif // _DEBUG
