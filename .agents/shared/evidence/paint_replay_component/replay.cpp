/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "native_report.h"
#include "test/test_paint_stream.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPluginLoader>
#include <QtCore/QSaveFile>
#include <QtGui/qpa/qplatformintegrationplugin.h>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <vector>

namespace PaintReplay {

constexpr auto kResultSchema = 1;
constexpr auto kTimedRole = "timed";
constexpr auto kAbsentRole = "absent";
constexpr auto kVersionAction
	= "build the component against the recording's exact Qt version";
constexpr auto kSidecarAction
	= "build the component from the recorder's test_paint_stream.h";
constexpr auto kAppearanceAction
	= "replay under the recorded platform and appearance, or record again";

struct Options {
	QString manifest;
	QString out;
	int warmup = 0;
	int samples = 0;
	QStringList fonts;
};

struct Failure {
	QString code;
	QString detail;
};

struct ContractCheck {
	const char *key = nullptr;
	const char *code = nullptr;
	const char *action = nullptr;
};

struct Row {
	QJsonObject manifest;
	Test::LoadedPaintStream stream;
	QJsonObject result;
	QImage replay;
};

[[nodiscard]] std::optional<int> ParseCount(
		const QString &value,
		int minimum) {
	auto ok = false;
	const auto result = value.toInt(&ok);
	return (ok && result >= minimum)
		? std::make_optional(result)
		: std::nullopt;
}

[[nodiscard]] std::optional<Options> ParseOptions(int argc, char **argv) {
	auto result = Options();
	auto seen = std::set<QString>();
	for (auto i = 1; i < argc; i += 2) {
		if (i + 1 >= argc) {
			return std::nullopt;
		}
		const auto key = QString::fromLocal8Bit(argv[i]);
		const auto value = QString::fromLocal8Bit(argv[i + 1]);
		if (key == QStringLiteral("--font")) {
			result.fonts.push_back(value);
			continue;
		}
		if (!seen.insert(key).second) {
			return std::nullopt;
		}
		if (key == QStringLiteral("--manifest")) {
			result.manifest = value;
		} else if (key == QStringLiteral("--out")) {
			result.out = value;
		} else if (key == QStringLiteral("--warmup")) {
			const auto count = ParseCount(value, 0);
			if (!count) {
				return std::nullopt;
			}
			result.warmup = *count;
		} else if (key == QStringLiteral("--samples")) {
			const auto count = ParseCount(value, 1);
			if (!count) {
				return std::nullopt;
			}
			result.samples = *count;
		} else {
			return std::nullopt;
		}
	}
	for (const auto &required : {
			QStringLiteral("--manifest"),
			QStringLiteral("--out"),
			QStringLiteral("--warmup"),
			QStringLiteral("--samples") }) {
		if (!seen.contains(required)) {
			return std::nullopt;
		}
	}
	if (result.manifest.isEmpty() || result.out.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] bool SamePath(const QString &a, const QString &b) {
	const auto first = QFileInfo(a);
	const auto second = QFileInfo(b);
	if (first.absoluteFilePath() == second.absoluteFilePath()) {
		return true;
	}
	const auto canonical = first.canonicalFilePath();
	return !canonical.isEmpty() && (canonical == second.canonicalFilePath());
}

[[nodiscard]] QString Sha256(const QByteArray &bytes) {
	return QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

[[nodiscard]] QJsonObject NativeJson() {
	return QJsonObject{
		{
			QStringLiteral("buildArch"),
			QString::fromLatin1(NativeReport::BuildArchitecture()),
		},
		{
			QStringLiteral("runningArch"),
			QString::fromStdString(NativeReport::RunningArchitecture()),
		},
		{ QStringLiteral("translated"), NativeReport::Translated() },
	};
}

[[nodiscard]] QJsonArray SentinelsJson() {
	auto result = QJsonArray();
	for (const auto index : { 0, 1 }) {
		const auto color = Test::PaintSentinel(index);
		result.push_back(
			QJsonArray{ color.red(), color.green(), color.blue() });
	}
	return result;
}

[[nodiscard]] QJsonArray RectJson(const QRect &rect) {
	return QJsonArray{ rect.x(), rect.y(), rect.width(), rect.height() };
}

[[nodiscard]] QString ValueText(const QJsonValue &value) {
	return value.isUndefined()
		? QStringLiteral("(missing)")
		: value.toVariant().toString();
}

[[nodiscard]] std::optional<Failure> SchemaFailure(
		const QJsonObject &manifest) {
	const auto fail = [](const QString &what) {
		return std::make_optional(Failure{
			QStringLiteral("manifest-schema"),
			what
				+ QStringLiteral("; pass a manifest.json written by")
				+ QStringLiteral(" test_paint_stream.h WritePaintManifest()"),
		});
	};
	const auto schema = manifest.value(QStringLiteral("schema"));
	if (schema.toInt(-1) != Test::kPaintStreamSchema) {
		return fail(QStringLiteral("schema is ") + ValueText(schema));
	}
	const auto qt = manifest.value(QStringLiteral("qt")).toObject();
	for (const auto key : { "version", "platform", "colorScheme" }) {
		if (qt.value(QString::fromLatin1(key)).toString().isEmpty()) {
			return fail(QStringLiteral("qt.%1 is not a string").arg(
				QString::fromLatin1(key)));
		}
	}
	for (const auto key : { "pictureFormat", "sidecar" }) {
		if (!qt.value(QString::fromLatin1(key)).isDouble()) {
			return fail(QStringLiteral("qt.%1 is not a number").arg(
				QString::fromLatin1(key)));
		}
	}
	const auto surface = Test::PaintSurfaceFromJson(
		manifest.value(QStringLiteral("surface")).toObject());
	if (surface.rootType.isEmpty()
		|| surface.size.isEmpty()
		|| !std::isfinite(surface.dpr)
		|| !(surface.dpr > 0.)
		|| Test::PaintTarget(surface, Test::PaintSentinel(0)).isNull()) {
		return fail(QStringLiteral("surface is not a replayable surface"));
	}
	if (manifest.value(QStringLiteral("sentinels")).toArray()
		!= SentinelsJson()) {
		return fail(QStringLiteral("sentinels differ from this replayer's"));
	}
	const auto streams = manifest.value(QStringLiteral("streams"));
	if (!streams.isArray()) {
		return fail(QStringLiteral("streams is not an array"));
	}
	for (const auto &row : streams.toArray()) {
		if (!row.isObject()) {
			return fail(QStringLiteral("a streams entry is not an object"));
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Failure> ReadManifest(
		const QString &path,
		QJsonObject *manifest,
		QJsonObject *identity) {
	const auto unreadable = [&](const QString &why) {
		return std::make_optional(Failure{
			QStringLiteral("manifest-unreadable"),
			path + QStringLiteral(": ") + why,
		});
	};
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return unreadable(file.errorString());
	}
	const auto bytes = file.readAll();
	if (file.error() != QFileDevice::NoError) {
		return unreadable(file.errorString());
	}
	*identity = QJsonObject{
		{ QStringLiteral("path"), path },
		{ QStringLiteral("bytes"), qint64(bytes.size()) },
		{ QStringLiteral("sha256"), Sha256(bytes) },
	};
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError) {
		return unreadable(parse.errorString());
	} else if (!document.isObject()) {
		return unreadable(QStringLiteral("not a JSON object"));
	}
	*manifest = document.object();
	return SchemaFailure(*manifest);
}

[[nodiscard]] QStringList IncludedPlatforms() {
	const auto iid = QString::fromLatin1(
		QPlatformIntegrationFactoryInterface_iid);
	auto result = QStringList();
	for (const auto &plugin : QPluginLoader::staticPlugins()) {
		const auto meta = plugin.metaData();
		if (meta.value(QStringLiteral("IID")).toString() != iid) {
			continue;
		}
		const auto keys = meta.value(QStringLiteral("MetaData"))
			.toObject()
			.value(QStringLiteral("Keys"))
			.toArray();
		for (const auto &key : keys) {
			result.push_back(key.toString());
		}
	}
	return result;
}

[[nodiscard]] std::optional<Failure> PlatformFailure(
		const QString &platform) {
	const auto included = IncludedPlatforms();
	if (included.contains(platform)) {
		return std::nullopt;
	}
	return Failure{
		QStringLiteral("platform-mismatch"),
		QStringLiteral("qt.platform is %1 in the manifest, and this replayer")
			.arg(platform)
			+ QStringLiteral(" includes only %1; ").arg(
				included.join(QStringLiteral(", ")))
			+ QString::fromLatin1(kAppearanceAction),
	};
}

[[nodiscard]] std::optional<Failure> ContractFailure(
		const QJsonObject &recorded,
		const QJsonObject &replayer) {
	const auto checks = std::array{
		ContractCheck{ "version", "qt-version-mismatch", kVersionAction },
		ContractCheck{
			"pictureFormat",
			"picture-format-mismatch",
			kVersionAction,
		},
		ContractCheck{ "sidecar", "sidecar-version-mismatch", kSidecarAction },
		ContractCheck{ "platform", "platform-mismatch", kAppearanceAction },
		ContractCheck{ "colorScheme", "platform-mismatch", kAppearanceAction },
	};
	for (const auto &check : checks) {
		const auto key = QString::fromLatin1(check.key);
		const auto was = recorded.value(key);
		const auto is = replayer.value(key);
		if (was == is) {
			continue;
		}
		return Failure{
			QString::fromLatin1(check.code),
			QStringLiteral("qt.%1 is %2 in the manifest and %3 here; ").arg(
				key,
				ValueText(was),
				ValueText(is))
				+ QString::fromLatin1(check.action),
		};
	}
	return std::nullopt;
}

[[nodiscard]] QJsonArray LoadFonts(
		const QStringList &files,
		QStringList *failed) {
	auto result = QJsonArray();
	for (const auto &file : files) {
		const auto id = QFontDatabase::addApplicationFont(file);
		const auto families = (id >= 0)
			? QFontDatabase::applicationFontFamilies(id)
			: QStringList();
		if (families.isEmpty()) {
			failed->push_back(file);
		}
		result.push_back(QJsonObject{
			{ QStringLiteral("file"), file },
			{ QStringLiteral("loaded"), !families.isEmpty() },
			{
				QStringLiteral("families"),
				QJsonArray::fromStringList(families),
			},
		});
	}
	return result;
}

[[nodiscard]] QString Name(const Row &row) {
	return row.result.value(QStringLiteral("name")).toString();
}

[[nodiscard]] QString Role(const Row &row) {
	return row.result.value(QStringLiteral("role")).toString();
}

[[nodiscard]] bool Failed(const Row &row) {
	return row.result.contains(QStringLiteral("error"));
}

void SetError(Row &row, const QString &code) {
	row.result.insert(QStringLiteral("error"), code);
}

[[nodiscard]] Row LoadRow(const QString &dir, const QJsonObject &manifest) {
	auto result = Row();
	result.manifest = manifest;
	result.stream = Test::LoadPaintStream(dir, manifest);
	result.result = QJsonObject{
		{
			QStringLiteral("name"),
			manifest.value(QStringLiteral("name")).toString(),
		},
		{
			QStringLiteral("role"),
			manifest.value(QStringLiteral("role")).toString(),
		},
	};
	const auto keys = { QStringLiteral("qpic"), QStringLiteral("tables") };
	for (const auto &key : keys) {
		if (result.stream.identity.contains(key)) {
			result.result.insert(key, result.stream.identity.value(key));
		}
	}
	if (!result.stream.error.isEmpty()) {
		SetError(result, result.stream.error);
	}
	return result;
}

[[nodiscard]] QImage ReplayUntimed(
		Row &row,
		const Test::PaintSurface &surface,
		const QImage &plain) {
	auto playedFirst = false;
	auto playedSecond = false;
	auto first = Test::PaintReplay(
		row.stream.picture,
		surface,
		Test::PaintSentinel(0),
		&playedFirst);
	const auto second = Test::PaintReplay(
		row.stream.picture,
		surface,
		Test::PaintSentinel(1),
		&playedSecond);
	if (!playedFirst || !playedSecond) {
		SetError(row, QStringLiteral("playback-failed"));
		return QImage();
	}
	const auto surfacePixels = qint64(first.width()) * first.height();
	const auto uncovered = Test::PaintDifference(first, second, nullptr);
	auto &result = row.result;
	result.insert(
		QStringLiteral("replaySha256"),
		Test::PaintImageDigest(first));
	result.insert(
		QStringLiteral("changedPixels"),
		Test::PaintDifference(first, plain, nullptr));
	result.insert(QStringLiteral("coveragePixels"), surfacePixels - uncovered);
	result.insert(QStringLiteral("surfacePixels"), surfacePixels);
	result.insert(
		QStringLiteral("fonts"),
		Test::PaintFonts(row.stream.picture));
	return first;
}

[[nodiscard]] QJsonObject PairJson(
		const Row &row,
		const QHash<QString, QImage> &references) {
	const auto with = row.manifest.value(QStringLiteral("pairedWith"))
		.toString();
	auto result = QJsonObject{ { QStringLiteral("with"), with } };
	const auto other = references.value(with);
	if (other.isNull()) {
		result.insert(QStringLiteral("error"), QStringLiteral("missing-pair"));
		return result;
	}
	auto bounds = QRect();
	result.insert(
		QStringLiteral("differingPixels"),
		Test::PaintDifference(row.replay, other, &bounds));
	result.insert(QStringLiteral("bounds"), RectJson(bounds));
	return result;
}

void TimeRow(Row &row, QImage &target, int warmup, int samples) {
	auto times = std::vector<qint64>();
	times.reserve(samples);
	const auto iterations = qint64(warmup) + samples;
	for (auto i = qint64(0); i != iterations; ++i) {
		target.fill(Test::PaintSentinel(0));
		auto played = false;
		auto timer = QElapsedTimer();
		timer.start();
		{
			auto painter = QPainter(&target);
			played = row.stream.picture.play(&painter);
		}
		const auto elapsed = timer.nsecsElapsed();
		if (!played) {
			SetError(row, QStringLiteral("playback-failed"));
			return;
		}
		if (i >= warmup) {
			times.push_back(elapsed);
		}
	}
	auto list = QJsonArray();
	for (const auto value : times) {
		list.push_back(value);
	}
	row.result.insert(QStringLiteral("timesNs"), list);
}

[[nodiscard]] QJsonArray ReplayRows(
		const QString &dir,
		const QJsonArray &streams,
		const Test::PaintSurface &surface,
		const Options &options) {
	const auto absent = QString::fromLatin1(kAbsentRole);
	const auto timed = QString::fromLatin1(kTimedRole);
	auto rows = std::vector<Row>();
	auto paired = std::set<QString>();
	for (const auto &value : streams) {
		rows.push_back(LoadRow(dir, value.toObject()));
		const auto &row = rows.back();
		if (Role(row) == absent) {
			paired.insert(
				row.manifest.value(QStringLiteral("pairedWith")).toString());
		}
	}
	const auto plain = Test::PaintTarget(surface, Test::PaintSentinel(0));
	auto references = QHash<QString, QImage>();
	for (auto &row : rows) {
		if (Failed(row)) {
			continue;
		}
		auto replay = ReplayUntimed(row, surface, plain);
		const auto name = Name(row);
		if (!replay.isNull()
			&& paired.contains(name)
			&& !references.contains(name)) {
			references.insert(name, replay);
		}
		if (Role(row) == absent) {
			row.replay = std::move(replay);
		}
	}
	for (auto &row : rows) {
		if (Role(row) == absent && !Failed(row)) {
			row.result.insert(
				QStringLiteral("pair"),
				PairJson(row, references));
		}
		row.replay = QImage();
	}
	references.clear();
	auto target = Test::PaintTarget(surface, Test::PaintSentinel(0));
	for (auto &row : rows) {
		if (Role(row) == timed && !Failed(row)) {
			TimeRow(row, target, options.warmup, options.samples);
		}
	}
	auto result = QJsonArray();
	for (const auto &row : rows) {
		result.push_back(row.result);
	}
	return result;
}

[[nodiscard]] bool WriteResult(const QString &path, const QJsonObject &result) {
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return false;
	}
	const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
	auto file = QSaveFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(bytes) == bytes.size())
		&& file.commit();
}

[[nodiscard]] int Finish(
		QJsonObject result,
		const QString &out,
		const std::optional<Failure> &failure) {
	if (failure) {
		result.insert(QStringLiteral("error"), failure->code);
		result.insert(QStringLiteral("errorDetail"), failure->detail);
		std::fprintf(
			stderr,
			"%s: %s\n",
			failure->code.toLocal8Bit().constData(),
			failure->detail.toLocal8Bit().constData());
	}
	result.insert(
		QStringLiteral("finishedAt"),
		QDateTime::currentMSecsSinceEpoch());
	if (!WriteResult(out, result)) {
		std::fprintf(
			stderr,
			"cannot write %s\n",
			out.toLocal8Bit().constData());
		return 1;
	}
	const auto rows = result.value(QStringLiteral("rows")).toArray();
	auto rowErrors = 0;
	for (const auto &row : rows) {
		if (row.toObject().contains(QStringLiteral("error"))) {
			++rowErrors;
		}
	}
	std::printf(
		"replay=%s rows=%d rowErrors=%d error=%s\n",
		out.toLocal8Bit().constData(),
		int(rows.size()),
		rowErrors,
		failure ? failure->code.toLocal8Bit().constData() : "none");
	return failure ? 1 : 0;
}

[[nodiscard]] int Run(
		const Options &options,
		char *program,
		qint64 startedAt) {
	auto result = QJsonObject{
		{ QStringLiteral("schema"), kResultSchema },
		{ QStringLiteral("tool"), QStringLiteral("paint_replay") },
		{ QStringLiteral("startedAt"), startedAt },
		{ QStringLiteral("native"), NativeJson() },
		{ QStringLiteral("warmup"), options.warmup },
		{ QStringLiteral("samples"), options.samples },
	};
	auto manifest = QJsonObject();
	auto identity = QJsonObject();
	auto failure = ReadManifest(options.manifest, &manifest, &identity);
	if (!identity.isEmpty()) {
		result.insert(QStringLiteral("manifest"), identity);
	}
	const auto recorded = manifest.value(QStringLiteral("qt")).toObject();
	const auto platform = recorded.value(QStringLiteral("platform"))
		.toString();
	if (!failure) {
		failure = PlatformFailure(platform);
	}
	auto argc = 1;
	char *argv[] = { program, nullptr };
	if (failure) {
		auto app = QCoreApplication(argc, argv);
		result.insert(
			QStringLiteral("executable"),
			QCoreApplication::applicationFilePath());
		return Finish(result, options.out, failure);
	}
	qputenv("QT_QPA_PLATFORM", platform.toLatin1());
	qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
	auto app = QGuiApplication(argc, argv);
	result.insert(
		QStringLiteral("executable"),
		QCoreApplication::applicationFilePath());
	const auto qt = Test::PaintQtIdentity();
	result.insert(QStringLiteral("qt"), qt);
	auto missing = QStringList();
	result.insert(QStringLiteral("fonts"), LoadFonts(options.fonts, &missing));
	const auto surface = Test::PaintSurfaceFromJson(
		manifest.value(QStringLiteral("surface")).toObject());
	result.insert(QStringLiteral("surface"), Test::PaintSurfaceJson(surface));
	failure = ContractFailure(recorded, qt);
	if (!failure && !missing.isEmpty()) {
		failure = Failure{
			QStringLiteral("font-load-failed"),
			missing.join(QStringLiteral(", "))
				+ QStringLiteral(" did not load; pass the font files the")
				+ QStringLiteral(" recording used, readable by QFontDatabase"),
		};
	}
	if (failure) {
		return Finish(result, options.out, failure);
	}
	result.insert(
		QStringLiteral("rows"),
		ReplayRows(
			QFileInfo(options.manifest).absolutePath(),
			manifest.value(QStringLiteral("streams")).toArray(),
			surface,
			options));
	return Finish(result, options.out, std::nullopt);
}

} // namespace PaintReplay

int main(int argc, char **argv) {
	if (NativeReport::Requested(argc, argv)) {
		return NativeReport::Print(argv[2]);
	}
	const auto startedAt = QDateTime::currentMSecsSinceEpoch();
	const auto options = PaintReplay::ParseOptions(argc, argv);
	if (!options) {
		std::fprintf(
			stderr,
			"usage: %s --manifest FILE --out FILE --warmup N --samples N"
			" [--font FILE]...\n",
			argv[0]);
		return 2;
	}
	if (PaintReplay::SamePath(options->manifest, options->out)) {
		std::fprintf(stderr, "--out must not name the --manifest file\n");
		return 2;
	}
	return PaintReplay::Run(*options, argv[0], startedAt);
}
