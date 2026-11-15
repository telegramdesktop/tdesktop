/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "native_report.h"
#include "test/test_paint_stream.h"

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtGui/QFontDatabase>
#include <QtGui/QLinearGradient>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <vector>

namespace PaintFixture {

constexpr auto kOwnerType = "PaintFixture::Owner";
constexpr auto kOwnerSize = QSize(240, 160);
constexpr auto kOwnerPosition = QPoint(8, 8);
constexpr auto kContainerSize = QSize(256, 176);
constexpr auto kRatio = 2.;
constexpr auto kFieldRect = QRect(16, 16, 64, 64);
constexpr auto kPixmapRect = QRect(176, 16, 48, 48);
constexpr auto kFrameRect = QRect(176, 72, 25, 16);
constexpr auto kFrameRowAlignment = 64;
constexpr auto kElementTop = 96.;
constexpr auto kElementSize = QSizeF(40., 24.);
constexpr auto kElementRadius = 6.;
constexpr auto kHairlineFrom = QPointF(96., 24.);
constexpr auto kHairlineTill = QPointF(160., 72.);
constexpr auto kTextPosition = QPointF(96., 146.);
constexpr auto kTextPixelSize = 14;
constexpr auto kBand = QRect(0, 0, 24, 160);
constexpr auto kRestX = 24.;
constexpr auto kGlideStartX = 72.;
constexpr auto kReleaseAtMs = 2100.;
constexpr auto kGlideVelocityPxPerMs = 0.4;
constexpr auto kGlideStopVelocityPxPerMs = 0.02;
constexpr auto kGlideTauMs = 100.;
constexpr auto kGlideFirstMs = 16.;
constexpr auto kGlideGapMs = 44.;

struct Options {
	QString out;
	QString font;
	bool container = false;
	bool translucent = false;
	bool band = false;
};

struct Sample {
	QString name;
	QString window;
	double at = 0.;
	bool pressed = false;
	bool moving = false;
	double x = 0.;
};

class Owner final : public QWidget {
public:
	Owner(QWidget *parent, const QString &family, bool opaque);

	void setField(bool shown);
	void setElement(double x);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _family;
	QImage _field;
	QImage _frame;
	QPixmap _pixmap;
	double _elementX = 0.;
	bool _opaque = true;
	bool _fieldShown = true;

};

class Container final : public QWidget {
public:
	Container();

};

[[nodiscard]] QImage FieldImage() {
	auto result = QImage(
		(QSizeF(kFieldRect.size()) * kRatio).toSize(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(kRatio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(200, 30, 90, 160));
	p.drawEllipse(QRectF(QPointF(), QSizeF(kFieldRect.size())));
	p.end();
	return result;
}

[[nodiscard]] QPixmap OpaquePixmap() {
	auto image = QImage(
		(QSizeF(kPixmapRect.size()) * kRatio).toSize(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(kRatio);
	image.fill(QColor(10, 160, 40));
	const auto half = QSizeF(kPixmapRect.size()) / 2.;
	auto p = QPainter(&image);
	p.fillRect(QRectF(QPointF(), half), QColor(240, 120, 20));
	p.fillRect(
		QRectF(QPointF(half.width(), half.height()), half),
		QColor(30, 60, 200));
	p.end();
	return QPixmap::fromImage(image);
}

void FreeFrame(void *buffer) {
	std::free(buffer);
}

[[nodiscard]] QImage PaddedFrame() {
	const auto size = (QSizeF(kFrameRect.size()) * kRatio).toSize();
	const auto perLine = (size.width() * 4 + kFrameRowAlignment - 1)
		/ kFrameRowAlignment
		* kFrameRowAlignment;
	const auto buffer = static_cast<uchar*>(
		std::calloc(std::size_t(perLine) * size.height(), 1));
	auto result = QImage(
		buffer,
		size.width(),
		size.height(),
		perLine,
		QImage::Format_ARGB32_Premultiplied,
		FreeFrame,
		buffer);
	result.setDevicePixelRatio(kRatio);
	result.fill(QColor(120, 60, 200));
	auto p = QPainter(&result);
	p.fillRect(
		QRectF(QPointF(), QSizeF(kFrameRect.size()) / 2.),
		QColor(250, 250, 250));
	p.end();
	return result;
}

[[nodiscard]] std::optional<Options> ParseOptions(int argc, char **argv) {
	auto result = Options();
	auto seen = std::set<QString>();
	for (auto i = 1; i < argc; i += 2) {
		const auto key = QString::fromLocal8Bit(argv[i]);
		if (i + 1 >= argc || !seen.insert(key).second) {
			return std::nullopt;
		}
		const auto value = QString::fromLocal8Bit(argv[i + 1]);
		const auto rootValid = (value == QStringLiteral("owner"))
			|| (value == QStringLiteral("container"));
		const auto ownerValid = (value == QStringLiteral("opaque"))
			|| (value == QStringLiteral("translucent"));
		const auto regionValid = (value == QStringLiteral("whole"))
			|| (value == QStringLiteral("band"));
		if (key == QStringLiteral("--out")) {
			result.out = value;
		} else if (key == QStringLiteral("--font")) {
			result.font = value;
		} else if (key == QStringLiteral("--root") && rootValid) {
			result.container = (value == QStringLiteral("container"));
		} else if (key == QStringLiteral("--owner") && ownerValid) {
			result.translucent = (value == QStringLiteral("translucent"));
		} else if (key == QStringLiteral("--timed-region") && regionValid) {
			result.band = (value == QStringLiteral("band"));
		} else {
			return std::nullopt;
		}
	}
	if (result.out.isEmpty() || result.font.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

void AddSample(
		std::vector<Sample> &list,
		const QString &window,
		double at,
		bool pressed,
		bool moving,
		double x) {
	const auto same = [&](const Sample &sample) {
		return (sample.window == window);
	};
	const auto index = std::count_if(list.begin(), list.end(), same);
	list.push_back({
		window + QStringLiteral("_") + QString::number(index),
		window,
		at,
		pressed,
		moving,
		x,
	});
}

[[nodiscard]] std::vector<Sample> TimedSamples() {
	auto result = std::vector<Sample>();
	const auto rest = QStringLiteral("rest");
	for (const auto at : { 1000., 1055., 1219. }) {
		AddSample(result, rest, at, false, false, kRestX);
	}
	const auto drag = QStringLiteral("drag");
	for (const auto &[at, x] : {
			std::pair{ 2000., 24. },
			std::pair{ 2016., 40. },
			std::pair{ 2032., 56. },
			std::pair{ 2048., 72. } }) {
		AddSample(result, drag, at, true, true, x);
	}
	const auto glide = QStringLiteral("glide");
	for (auto t = kGlideFirstMs; ; t += kGlideGapMs) {
		const auto decay = std::exp(-t / kGlideTauMs);
		const auto moving = (kGlideVelocityPxPerMs * decay
			>= kGlideStopVelocityPxPerMs);
		const auto travel = moving
			? (kGlideVelocityPxPerMs * kGlideTauMs * (1. - decay))
			: ((kGlideVelocityPxPerMs - kGlideStopVelocityPxPerMs)
				* kGlideTauMs);
		AddSample(
			result,
			glide,
			kReleaseAtMs + t,
			false,
			moving,
			kGlideStartX + travel);
		if (!moving) {
			break;
		}
	}
	return result;
}

[[nodiscard]] QJsonObject SampleFields(const Sample &sample) {
	return QJsonObject{
		{ QStringLiteral("window"), sample.window },
		{ QStringLiteral("at"), sample.at },
		{ QStringLiteral("pressed"), sample.pressed },
		{ QStringLiteral("moving"), sample.moving },
		{ QStringLiteral("position"), QJsonArray{ sample.x, kElementTop } },
	};
}

[[nodiscard]] QJsonArray RectJson(const QRect &rect) {
	return QJsonArray{ rect.x(), rect.y(), rect.width(), rect.height() };
}

void PaintOnce(QWidget *root, const Test::PaintSurface &surface) {
	auto target = Test::PaintTarget(surface, Test::PaintSentinel(0));
	auto painter = QPainter(&target);
	root->render(&painter);
}

[[nodiscard]] int Fail(const QString &message) {
	std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
	return 3;
}

Owner::Owner(QWidget *parent, const QString &family, bool opaque)
: QWidget(parent)
, _family(family)
, _field(FieldImage())
, _frame(PaddedFrame())
, _pixmap(OpaquePixmap())
, _opaque(opaque) {
	if (_opaque) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}
	setGeometry(QRect(kOwnerPosition, kOwnerSize));
}

void Owner::setField(bool shown) {
	_fieldShown = shown;
}

void Owner::setElement(double x) {
	_elementX = x;
}

void Owner::paintEvent(QPaintEvent *) {
	auto p = QPainter(this);
	if (_opaque) {
		auto gradient = QLinearGradient(QPointF(), QPointF(width(), height()));
		gradient.setColorAt(0., QColor(240, 240, 235));
		gradient.setColorAt(1., QColor(180, 200, 230));
		p.fillRect(rect(), gradient);
	}
	p.drawPixmap(kPixmapRect.topLeft(), _pixmap);
	p.drawImage(kFrameRect.topLeft(), _frame);
	if (_fieldShown) {
		p.drawImage(kFieldRect.topLeft(), _field);
	}
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(QColor(20, 40, 200), 2.));
	p.setBrush(QColor(250, 200, 0));
	p.drawRoundedRect(
		QRectF(QPointF(_elementX, kElementTop), kElementSize),
		kElementRadius,
		kElementRadius);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setPen(QPen(QColor(30, 30, 30), 0.));
	p.drawLine(kHairlineFrom, kHairlineTill);
	auto font = QFont(_family);
	font.setPixelSize(kTextPixelSize);
	p.setFont(font);
	p.setPen(QColor(20, 20, 20));
	p.drawText(kTextPosition, QStringLiteral("Paint replay 0123"));
}

Container::Container() {
	setAttribute(Qt::WA_NoSystemBackground);
	resize(kContainerSize);
}

[[nodiscard]] int Run(const Options &options) {
	const auto fontId = QFontDatabase::addApplicationFont(options.font);
	const auto families = QFontDatabase::applicationFontFamilies(fontId);
	if (fontId < 0 || families.isEmpty()) {
		return Fail(QStringLiteral("font-load-failed: ") + options.font);
	}
	auto container = Container();
	auto owner = Owner(&container, families.front(), !options.translucent);
	const auto ownerType = Test::PaintTypeName(&owner);
	if (ownerType != QString::fromLatin1(kOwnerType)) {
		return Fail(QStringLiteral("owner type reads ") + ownerType);
	}
	const auto platform = QGuiApplication::platformName();
	if (platform != QStringLiteral("cocoa")) {
		return Fail(QStringLiteral("platform is ")
			+ platform
			+ QStringLiteral(", the recorder mirrors Telegram's cocoa;")
			+ QStringLiteral(" unset QT_QPA_PLATFORM"));
	}
	auto surface = Test::PaintSurface();
	surface.rootType = QString::fromLatin1(kOwnerType);
	surface.size = kOwnerSize;
	surface.dpr = kRatio;
	const auto root = options.container
		? static_cast<QWidget*>(&container)
		: static_cast<QWidget*>(&owner);
	const auto region = options.band ? QRegion(kBand) : QRegion();
	PaintOnce(&owner, surface);
	const auto timed = QStringLiteral("timed");
	auto recordings = std::vector<Test::PaintRecording>();
	for (const auto &sample : TimedSamples()) {
		owner.setElement(sample.x);
		auto recording = Test::RecordPaint(
			root,
			sample.name,
			timed,
			SampleFields(sample),
			region);
		recording.directDifference = Test::PaintDirectDifference(
			root,
			recording,
			surface);
		recordings.push_back(std::move(recording));
	}
	owner.setElement(kRestX);
	owner.setField(false);
	recordings.push_back(Test::RecordPaint(
		root,
		QStringLiteral("absent_0"),
		QStringLiteral("absent"),
		QJsonObject{
			{ QStringLiteral("pairedWith"), QStringLiteral("rest_0") },
			{ QStringLiteral("fieldRect"), RectJson(kFieldRect) },
		},
		region));
	owner.setField(true);
	recordings.push_back(Test::RecordPaint(
		&owner,
		QStringLiteral("narrow_0"),
		QStringLiteral("narrow"),
		QJsonObject(),
		QRegion(kBand)));
	recordings.push_back(Test::EmptyPaintRecording(QStringLiteral("empty_0")));
	auto rows = QJsonArray();
	auto error = QString();
	for (const auto &recording : recordings) {
		const auto row = Test::SavePaintRecording(
			options.out,
			recording,
			surface,
			&error);
		if (!error.isEmpty()) {
			return Fail(error);
		}
		rows.push_back(row);
	}
	if (!Test::WritePaintManifest(options.out, surface, rows, &error)) {
		return Fail(error);
	}
	const auto manifest = QDir(options.out).filePath(
		QStringLiteral("manifest.json"));
	std::printf(
		"manifest=%s streams=%d\n",
		manifest.toLocal8Bit().constData(),
		int(rows.size()));
	return 0;
}

} // namespace PaintFixture

int main(int argc, char **argv) {
	if (NativeReport::Requested(argc, argv)) {
		return NativeReport::Print(argv[2]);
	}
	const auto options = PaintFixture::ParseOptions(argc, argv);
	if (!options) {
		std::fprintf(
			stderr,
			"usage: %s --out DIR --font FILE [--root owner|container]"
			" [--owner opaque|translucent] [--timed-region whole|band]\n",
			argv[0]);
		return 2;
	}
	qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
	auto app = QApplication(argc, argv);
	return PaintFixture::Run(*options);
}
