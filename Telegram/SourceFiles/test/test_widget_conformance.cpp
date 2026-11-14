/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance.h"

#include "core/version.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_widget.h"
#include "main/main_session.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_launch_fuse.h"
#include "test/test_log.h"
#include "test/test_palette.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "test/test_widget_bot.h"
#include "test/test_widget_conformance_actions.h"
#include "test/test_widget_conformance_bridge.h"
#include "test/test_widget_conformance_context.h"
#include "test/test_widget_conformance_hosts.h"
#include "test/test_widget_conformance_paint_policy.h"
#include "test/test_widget_conformance_pictures.h"
#include "test/test_widget_conformance_state.h"
#include "test/test_widget_conformance_stop.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"
#include "test/test_widget_ops.h"
#include "tgw/tgw_intl.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/fields/input_field.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QSize>
#include <QtCore/QSysInfo>
#include <QtCore/QtGlobal>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QWidget>

#include <rpl/lifetime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Test::Conformance {
namespace {

using RuntimeState = Tgw::Runtime::State;

constexpr auto kPrefix = "widget conformance: ";

// Every run's width anchor and expected card width, in CSS px: the width
// CLIENTS.md asks for. A measured core width further from it than the
// tolerance is a runner refusal.
constexpr auto kWidth = 390;
constexpr auto kWidthTolerance = 0.01;

// CLIENTS.md's bound on a widget's first result, from the load show.
constexpr auto kResultBound = crl::time(20000);

// The top band of the card the load show brings into view, in CSS px:
// enough for the list to count the card visible, so its host starts, and
// never taller than the viewport, whatever height the widget grows to.
constexpr auto kLoadBandCss = 48.;

// CLIENTS.md's bound on done after a run's actions.
constexpr auto kDoneBound = crl::time(10000);

// The runtime log is bounded by the product (kLogBytes); its row keeps the
// head, which holds the state and diagnostic kinds.
constexpr auto kMaxLogChars = 32768;

// The one run the requested matrix (every auto test in both themes)
// leaves out, and only while its manifest entry still gives the reason
// OmissionReason() states.
constexpr auto kOmittedTest = "auto/20-client-behaviour";

// What DefaultHostOptions() in tgw_runtime.cpp gives every widget host;
// the runner configures no host of its own.
constexpr auto kPolicy = "strict";
constexpr auto kFeature = "webAnimations";

// No stage rect of a card at most 600 CSS px tall comes near this; a
// larger number is a malformed manifest entry.
constexpr auto kMaxCss = 4096.;

// The test-only private-member accessor of test_chat_show.cpp, kept
// file-private here: HistoryWidget's compose field, the id of the message
// it edits and its voice record bar. Every use only reads.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct HistoryFieldTag {
	using type = object_ptr<Ui::InputField> HistoryWidget::*;
	friend type Get(HistoryFieldTag);
};

template struct PrivateMember<HistoryFieldTag, &HistoryWidget::_field>;

struct HistoryEditTag {
	using type = MsgId HistoryWidget::*;
	friend type Get(HistoryEditTag);
};

template struct PrivateMember<HistoryEditTag, &HistoryWidget::_editMsgId>;

struct HistoryVoiceTag {
	using type = const std::unique_ptr<HistoryView::Controls::VoiceRecordBar>
		HistoryWidget::*;
	friend type Get(HistoryVoiceTag);
};

template struct PrivateMember<
	HistoryVoiceTag,
	&HistoryWidget::_voiceRecordBar>;

// The run counts of the whole manifest and of this selection.
// |permanentAuto| counts the manifest's auto runs omitted in every launch,
// |elsewhereAuto| and |elsewhereVisual| this selection's runs left to a
// launch on another chat.
struct Matrix {
	int autoTests = 0;
	int visualTests = 0;
	int clientsAutoRuns = 0;
	int visualManifestRuns = 0;
	int permanentAuto = 0;
	int elsewhereAuto = 0;
	int elsewhereVisual = 0;
	int autoLight = 0;
	int autoDark = 0;
	int extraDark = 0;
	int visualLight = 0;
	int visualDark = 0;
};

[[nodiscard]] QJsonArray Strings(const std::vector<std::string> &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(QString::fromStdString(entry));
	}
	return result;
}

[[nodiscard]] QJsonArray Strings(const QStringList &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(entry);
	}
	return result;
}

// Takes |latest| into |result| when its generation or serial is new and
// returns whether it did. The value counts only when it is a JSON object
// published for |id| with a checks array.
bool UpdateResult(
		ConformanceResult &result,
		const WidgetOpsLatest &latest,
		const QString &id) {
	if (result.read
		&& (result.generation == latest.generation)
		&& (result.serial == latest.serial)) {
		return false;
	}
	result = ConformanceResult{
		.htmlId = latest.adopted ? latest.htmlId : 0,
		.generation = latest.generation,
		.serial = latest.serial,
		.updates = latest.updates,
		.read = true,
	};
	if (!latest.conformance) {
		return true;
	}
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		QByteArray::fromStdString(*latest.conformance),
		&parse);
	const auto object = document.object();
	const auto published = object.value(u"id"_q).toString();
	const auto checks = object.value(u"checks"_q);
	if (parse.error != QJsonParseError::NoError) {
		result.error = u"the newest value is not JSON: "_q
			+ parse.errorString();
	} else if (!document.isObject()) {
		result.error = u"the newest value is not a JSON object"_q;
	} else if (published != id) {
		result.error = u"the newest value is published for [%1], not for "
			u"this run's %2"_q.arg(published, id);
	} else if (!checks.isArray()) {
		result.error = u"the newest value's checks are not an array"_q;
	} else {
		result.value = object;
		result.checks = checks.toArray();
		result.targets = object.value(u"targets"_q).toObject();
		result.status = object.value(u"status"_q).toString();
		result.present = true;
	}
	return true;
}

// Why the run's theme is not shown: its phase's palette switch refused.
[[nodiscard]] QString PaletteSkip(
		const ConformanceState &state,
		const ConformanceRun &run) {
	if (run.palette < 0 || run.palette >= int(state.palette.size())) {
		return QString();
	}
	const auto &switched = *state.palette[run.palette].switched;
	return (switched.finished && !switched.succeeded())
		? u"not run: the palette switch to %1 for the %2 runs refused: "
			u"%3 - %4"_q.arg(
				PaletteModeName(switched.requested),
				run.theme,
				PaletteRefusalName(switched.refusal),
				switched.reason)
		: QString();
}

[[nodiscard]] QString FocusText(QWidget *widget) {
	return widget ? WidgetDescription(widget) : u"none"_q;
}

// The legacy HistoryWidget that owns the fixture's list and shows its
// history, or null.
[[nodiscard]] HistoryWidget *ComposeOwner(const ConformanceState &state) {
	const auto history = WidgetFixtureHistory(*state.fixture);
	auto widget = WidgetFixtureList(*state.fixture);
	while (widget) {
		if (const auto owner = dynamic_cast<HistoryWidget*>(widget)) {
			return (history && (owner->history() == history))
				? owner
				: nullptr;
		}
		widget = widget->parentWidget();
	}
	return nullptr;
}

// |text| with every character but ASCII letters, digits, '-' and '_'
// replaced, for a file name.
[[nodiscard]] QString FilePart(QString text) {
	for (auto &ch : text) {
		const auto plain = (ch.unicode() < 128)
			&& (ch.isLetterOrNumber()
				|| (ch == QChar('-'))
				|| (ch == QChar('_')));
		if (!plain) {
			ch = QChar('_');
		}
	}
	return text;
}

} // namespace

QString Prefixed(const QString &text) {
	return QString::fromLatin1(kPrefix) + text;
}

QString NumberText(qint64 value) {
	return QString::number(value);
}

QString CompactJson(const QJsonObject &object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// One machine-readable row: "NOTE: <marker>: <compact JSON>", every string
// escaped by QJsonDocument.
void Row(const QString &marker, const QJsonObject &fields) {
	Note(marker + u": "_q + CompactJson(fields));
}

QJsonArray RectArray(QRect rect) {
	return QJsonArray{ rect.x(), rect.y(), rect.width(), rect.height() };
}

QJsonObject ShowObject(const ChatListShow *shown) {
	if (!shown || !shown->finished) {
		return QJsonObject{ { u"finished"_q, false } };
	}
	return QJsonObject{
		{ u"finished"_q, true },
		{ u"shown"_q, shown->shown() },
		{ u"refusal"_q, ChatListShowRefusalName(shown->refusal) },
		{ u"reason"_q, shown->reason },
		{ u"local"_q, RectArray(shown->local) },
		{ u"rect"_q, RectArray(shown->rect) },
		{ u"viewport"_q, RectArray(shown->viewport) },
		{ u"paints"_q, shown->paints },
		{ u"elapsedMs"_q, qint64(shown->elapsedMs) },
	};
}

QString GlobalSkip(const ConformanceState &state) {
	const auto gate = state.fixture
		? WidgetFixtureGate(*state.fixture)
		: QString();
	return !state.packageError.isEmpty()
		? state.packageError
		: !state.outputError.isEmpty()
		? state.outputError
		: !gate.isEmpty()
		? gate
		: !state.watcherError.isEmpty()
		? state.watcherError
		: state.abort;
}

QString RunSkip(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto global = GlobalSkip(state);
	const auto palette = global.isEmpty()
		? PaletteSkip(state, run)
		: QString();
	return !global.isEmpty()
		? global
		: !palette.isEmpty()
		? palette
		: run.notRun;
}

bool WantedStatus(
		const ConformanceState &state,
		const ConformanceRun &run,
		ResultWait wait) {
	const auto &result = run.result;
	if (!result.present) {
		return false;
	}
	const auto &test = state.tests[run.test];
	if (!test.stops.isEmpty()) {
		return false;
	}
	const auto awaiting = (wait == ResultWait::Initial)
		&& ((test.set != u"auto"_q) || !test.actions.isEmpty());
	return (result.status == u"done"_q)
		|| (awaiting && (result.status == u"awaiting"_q));
}

void RefreshResult(ConformanceRun &run) {
	if (run.message
		&& run.message->tap
		&& UpdateResult(
			run.result,
			ReadWidgetOpsLatest(*run.message->tap),
			run.id)
		&& run.result.present) {
		run.retained = run.result;
	}
}

void AddRefusal(
		ConformanceRun &run,
		const QString &name,
		const QString &detail) {
	run.refusals.push_back({ .name = name, .detail = detail });
}

// Reads the compose field of the fixture's chat and the window's focus
// child; changes nothing. An unusual field is one an Enter could send
// from, or one holding someone's draft that a typed key would change.
ComposeReading ReadCompose(const ConformanceState &state) {
	auto result = ComposeReading();
	result.application = FocusText(QApplication::focusWidget());
	const auto owner = ComposeOwner(state);
	const auto history = WidgetFixtureHistory(*state.fixture);
	const auto field = owner
		? (owner->*Get(HistoryFieldTag())).data()
		: nullptr;
	if (!owner || !field || !history) {
		result.unusual.push_back(!owner
			? u"no legacy HistoryWidget showing the chat owns the fixture's "
				u"list"_q
			: u"its compose field is gone"_q);
		return result;
	}
	result.found = true;
	result.owner = WidgetDescription(owner);
	result.editor = field->rawTextEdit().get();
	result.length = int(field->getTextWithTags().text.size());
	const auto voice = (owner->*Get(HistoryVoiceTag())).get();
	const auto cloud = history->cloudDraft(MsgId(), PeerId());
	const auto unusual = std::vector<std::pair<bool, QString>>{
		{ field->isHidden(), u"the field is hidden"_q },
		{
			(result.length > 0),
			u"the field holds a draft of %1 characters"_q.arg(result.length),
		},
		{
			bool(owner->*Get(HistoryEditTag())),
			u"a message is being edited"_q,
		},
		{
			(history->localEditDraft(MsgId(), PeerId()) != nullptr),
			u"an edit draft is kept"_q,
		},
		{
			(cloud && cloud->hasRichMessage()),
			u"the cloud draft holds a rich message"_q,
		},
		{
			(voice && voice->isActive()),
			u"the voice record bar records or holds a recording to send"_q,
		},
		{
			!history->forwardDraft(MsgId(), PeerId()).ids.empty(),
			u"a forward draft is set, which an Enter would send"_q,
		},
	};
	for (const auto &[present, what] : unusual) {
		if (present) {
			result.unusual.push_back(what);
		}
	}
	const auto window = owner->window();
	const auto holder = window ? window->focusWidget() : nullptr;
	result.focus = holder;
	result.holder = FocusText(holder);
	result.active = window && window->isActiveWindow();
	result.focused = holder && (holder == result.editor.data());
	return result;
}

QJsonObject ComposeObject(const ComposeReading &compose) {
	return QJsonObject{
		{ u"found"_q, compose.found },
		{ u"owner"_q, compose.owner },
		{ u"ordinary"_q, compose.found && compose.unusual.isEmpty() },
		{ u"unusual"_q, Strings(compose.unusual) },
		{ u"fieldLength"_q, compose.length },
		{ u"focused"_q, compose.focused },
		{ u"windowFocus"_q, compose.holder },
		{ u"applicationFocus"_q, compose.application },
		{ u"windowActive"_q, compose.active },
	};
}

Ui::InputField *ComposeField(const ConformanceState &state) {
	const auto owner = ComposeOwner(state);
	return owner ? (owner->*Get(HistoryFieldTag())).data() : nullptr;
}

QRect LoadRect(QRect card) {
	return QRect(
		card.x(),
		card.y(),
		card.width(),
		std::min(card.height(), Tgw::LogicalHeight(kLoadBandCss)));
}

void JudgeRestore(
		const std::shared_ptr<const PaletteSwitch> &switched,
		const std::shared_ptr<const PaletteRestore> &restore,
		const QString &what) {
	if (switched && switched->succeeded()) {
		Check(restore->restored(), what, PaletteRestoreText(*restore));
	} else {
		Note(what + u": not judged, its switch did not succeed - "_q
			+ PaletteRestoreText(*restore));
	}
}

bool CropScaleHolds() {
	return (style::DevicePixelRatio() * style::Scale() == kCropScale * 100);
}

// |css| in logical px, exact while CropScaleHolds(): the interface scale
// is then 100 or 200.
QRect LogicalFromCss(QRect css) {
	const auto scale = style::Scale();
	return QRect(
		css.x() * scale / 100,
		css.y() * scale / 100,
		css.width() * scale / 100,
		css.height() * scale / 100);
}

// Why no snapshot of the run can be taken at all, before any show.
RunnerRefusal SnapshotBlock(const ConformanceRun &run) {
	if (!CropScaleHolds()) {
		const auto ratio = style::DevicePixelRatio();
		const auto scale = style::Scale();
		return {
			.name = u"not-2x"_q,
			.detail = u"device pixel ratio %1 at interface scale %2% paints "
				u"%3 image px per CSS px, not %4, and no frame is "
				u"upscaled"_q.arg(
					NumberText(ratio),
					NumberText(scale),
					QString::number(ratio * scale / 100.),
					NumberText(kCropScale)),
		};
	} else if (run.died) {
		return {
			.name = u"not-live"_q,
			.detail = u"the widget died: "_q + run.dead,
		};
	} else if (!run.loaded) {
		return {
			.name = u"not-live"_q,
			.detail = u"the widget did not load"_q,
		};
	}
	return {};
}

QString FileBase(const ConformanceRun &run) {
	return FilePart(run.id) + u"."_q + run.theme;
}

// Saves |image| as a PNG at |relative| under the output directory and
// reads its size back from the file; empty, or what failed.
QString SavePng(
		const ConformanceState &state,
		const QImage &image,
		const QString &relative) {
	if (!state.outputReady) {
		return u"the output directory was never prepared"_q;
	}
	const auto path = QDir(state.outputDir).filePath(relative);
	const auto dir = QFileInfo(path).absolutePath();
	if (!QDir().mkpath(dir)) {
		return u"the directory [%1] could not be created"_q.arg(dir);
	} else if (!image.save(path, "PNG")) {
		return u"QImage::save() could not write [%1]"_q.arg(path);
	}
	auto reader = QImageReader(path, "png");
	const auto size = reader.size();
	if (size != image.size()) {
		return u"[%1] reads back as %2x%3, not %4x%5: %6"_q.arg(
			path,
			NumberText(size.width()),
			NumberText(size.height()),
			NumberText(image.width()),
			NumberText(image.height()),
			reader.errorString());
	}
	return QString();
}

namespace {

[[nodiscard]] QString IdText(FullMsgId id) {
	return id
		? u"%1:%2"_q.arg(
			NumberText(qint64(id.peer.value)),
			NumberText(qint64(id.msg.bare)))
		: u"none"_q;
}

[[nodiscard]] qint64 WallNow() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] QJsonValue OptionalNumber(std::optional<double> value) {
	return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

[[nodiscard]] WidgetOpsSelfCheckRow SelfCheckRow(
		bool passed,
		const QString &name,
		const QString &details) {
	return {
		.name = Prefixed(name),
		.details = details,
		.passed = passed,
	};
}

// ---------------------------------------------------------------------
// The package, read once while the stages are registered.

[[nodiscard]] QByteArray ReadBytes(const QString &path, QString *error) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		*error = file.errorString();
		return QByteArray();
	}
	return file.readAll();
}

// The JSON object |bytes| hold, or an empty one with |error| set.
[[nodiscard]] QJsonObject ParseObject(
		const QByteArray &bytes,
		QString *error) {
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError) {
		*error = u"not JSON: %1 at offset %2"_q.arg(
			parse.errorString(),
			NumberText(parse.offset));
	} else if (!document.isObject()) {
		*error = u"not a JSON object"_q;
	}
	return document.object();
}

[[nodiscard]] bool ThemeName(const QString &name) {
	return (name == u"light"_q) || (name == u"dark"_q);
}

[[nodiscard]] bool SetName(const QString &name) {
	return (name == u"auto"_q) || (name == u"visual"_q);
}

// The names of a JSON array of theme names, or an empty list when it is
// not one or names anything else.
[[nodiscard]] QStringList ThemeList(const QJsonValue &value) {
	auto result = QStringList();
	for (const auto &entry : value.toArray()) {
		if (!ThemeName(entry.toString())) {
			return QStringList();
		}
		result.push_back(entry.toString());
	}
	return result;
}

// [x, y, w, h] in whole, non-negative CSS px, with w and h positive.
[[nodiscard]] std::optional<QRect> StageRect(const QJsonValue &value) {
	const auto list = value.toArray();
	if (list.size() != 4) {
		return std::nullopt;
	}
	auto numbers = std::array<int, 4>();
	for (auto i = 0; i != 4; ++i) {
		const auto number = list[i].toDouble(-1.);
		if (!list[i].isDouble()
			|| (number < 0.)
			|| (number > kMaxCss)
			|| (std::floor(number) != number)) {
			return std::nullopt;
		}
		numbers[i] = int(number);
	}
	if (!numbers[2] || !numbers[3]) {
		return std::nullopt;
	}
	return QRect(numbers[0], numbers[1], numbers[2], numbers[3]);
}

[[nodiscard]] ManifestTest ParseTest(
		const QJsonValue &entry,
		QSet<QString> &ids) {
	const auto object = entry.toObject();
	auto result = ManifestTest{
		.id = object.value(u"id"_q).toString(),
		.set = object.value(u"set"_q).toString(),
		.group = object.value(u"group"_q).toString(),
		.file = object.value(u"file"_q).toString(),
		.themes = ThemeList(object.value(u"themes"_q)),
		.actions = object.value(u"actions"_q).toArray(),
		.host = object.value(u"host"_q).toArray(),
	};
	auto problems = QStringList();
	if (!entry.isObject()) {
		problems.push_back(u"not an object"_q);
	}
	if (result.id.isEmpty()) {
		problems.push_back(u"no id"_q);
	} else if (ids.contains(result.id)) {
		problems.push_back(u"a duplicate id"_q);
	}
	ids.insert(result.id);
	if (!SetName(result.set)) {
		problems.push_back(
			u"set [%1] is neither auto nor visual"_q.arg(result.set));
	}
	if (result.file.isEmpty()) {
		problems.push_back(u"no file"_q);
	}
	if (result.themes.isEmpty()) {
		problems.push_back(
			u"themes is not a non-empty list of light and dark"_q);
	}
	auto widthListed = false;
	for (const auto &width : object.value(u"widths"_q).toArray()) {
		widthListed = widthListed || (width.toDouble() == double(kWidth));
	}
	if (!widthListed) {
		problems.push_back(
			u"widths does not list %1"_q.arg(NumberText(kWidth)));
	}
	for (const auto &key : { u"actions"_q, u"stages"_q, u"host"_q }) {
		if (object.contains(key) && !object.value(key).isArray()) {
			problems.push_back(key + u" is not an array"_q);
		}
	}
	for (const auto &action : result.actions) {
		if (!action.isObject()) {
			problems.push_back(u"an action is not an object"_q);
			break;
		}
	}
	for (const auto &check : result.host) {
		if (!check.toObject().value(u"id"_q).isString()) {
			problems.push_back(u"a host check is not an object with an id"_q);
			break;
		}
	}
	result.stops = object.value(u"stops"_q).toString();
	if (object.contains(u"stops"_q) && result.stops.isEmpty()) {
		problems.push_back(u"stops is not a non-empty string"_q);
	}
	result.safeArea = object.contains(u"safeArea"_q);
	if (result.safeArea && !object.value(u"safeArea"_q).isObject()) {
		problems.push_back(u"safeArea is not an object"_q);
	}
	if (result.set == u"auto"_q) {
		for (const auto &value : object.value(u"stages"_q).toArray()) {
			const auto stage = value.toObject();
			const auto rect = StageRect(stage.value(u"rect"_q));
			auto parsed = ManifestStage{
				.name = stage.value(u"name"_q).toString(),
				.themes = ThemeList(stage.value(u"themes"_q)),
				.rect = rect ? *rect : QRect(),
			};
			if (parsed.name.isEmpty() || parsed.themes.isEmpty() || !rect) {
				problems.push_back(u"stage [%1] needs a name, themes of "
					u"light and dark and a rect in whole CSS px"_q.arg(
						parsed.name));
			}
			result.stages.push_back(std::move(parsed));
		}
	}
	ParseManifestMessage(object, result, problems);
	ParseManifestBot(object, result, problems);
	result.error = problems.join(u"; "_q);
	return result;
}

// Reads manifest.json and probe-fixture.json; refuses the whole package
// by name when either is missing or not what the runner reads.
void ReadPackage(ConformanceState &state) {
	const auto &dir = state.args.package;
	const auto info = QFileInfo(dir);
	if (dir.isEmpty() || !info.isAbsolute() || !info.isDir()) {
		state.packageError = u"the package directory [%1] is not an "
			u"absolute directory"_q.arg(dir);
		return;
	}
	auto error = QString();
	const auto manifestBytes = ReadBytes(
		QDir(dir).filePath(u"manifest.json"_q),
		&error);
	const auto manifest = error.isEmpty()
		? ParseObject(manifestBytes, &error)
		: QJsonObject();
	const auto tests = manifest.value(u"tests"_q);
	if (error.isEmpty() && !tests.isArray()) {
		error = u"no tests array"_q;
	}
	if (!error.isEmpty()) {
		state.packageError = u"manifest.json: "_q + error;
		return;
	}
	state.probeFixture = ReadBytes(
		QDir(dir).filePath(u"probe-fixture.json"_q),
		&error);
	const auto probe = error.isEmpty()
		? ParseObject(state.probeFixture, &error)
		: QJsonObject();
	if (error.isEmpty() && !probe.value(u"cases"_q).isArray()) {
		error = u"no cases array"_q;
	}
	if (!error.isEmpty()) {
		state.packageError = u"probe-fixture.json: "_q + error;
		return;
	}
	auto ids = QSet<QString>();
	for (const auto &entry : tests.toArray()) {
		state.tests.push_back(ParseTest(entry, ids));
	}
}

void ReadHtml(const ConformanceState &state, ManifestTest &test) {
	if (test.read) {
		return;
	}
	test.read = true;
	auto error = QString();
	test.html = ReadBytes(
		QDir(state.args.package).filePath(test.file),
		&error);
	if (!error.isEmpty()) {
		test.htmlError = u"unreadable: "_q + error;
	} else if (test.html.isEmpty()) {
		test.htmlError = u"empty: a widget message needs HTML"_q;
	}
}

// Why auto/20-client-behaviour's dark start is left out, while its entry
// still gives the reason: it declares light only and its first action
// switches the chat to dark itself. Empty for every other run.
[[nodiscard]] QString OmissionReason(
		const ManifestTest &test,
		const QString &theme) {
	const auto first = test.actions.isEmpty()
		? QJsonObject()
		: test.actions.first().toObject();
	if ((test.id != QString::fromLatin1(kOmittedTest))
		|| (theme != u"dark"_q)
		|| (test.themes != QStringList{ u"light"_q })
		|| (first.value(u"theme"_q).toString() != u"dark"_q)) {
		return QString();
	}
	return u"its header and manifest entry declare themes [\"light\"]: its "
		u"scenario starts in light and switches the chat to dark and back "
		u"itself, so in a dark start its first action (theme: dark) "
		u"changes nothing and its theme-switch check could only fail by "
		u"the runner's doing; the maintainers' web runner and twidget's "
		u"reference runner run it in light only"_q;
}

// Why |test| is left out of |theme| in every launch: auto/20's dark start,
// or an auto entry whose message is a preview. Empty for every other run.
[[nodiscard]] QString PermanentOmission(
		const ManifestTest &test,
		const QString &theme) {
	const auto reason = OmissionReason(test, theme);
	return reason.isEmpty() ? PreviewOmission(test) : reason;
}

// The kind of a manifest action: the first of CLIENTS.md's action keys
// it has, in CLIENTS.md's order (a QJsonObject keeps no key order).
[[nodiscard]] QString ActionKind(const QJsonValue &action) {
	const auto object = action.toObject();
	for (const auto kind : {
			"tap",
			"dblclick",
			"rightclick",
			"hover",
			"press",
			"drag",
			"key",
			"wheel",
			"theme",
			"visible",
			"wait" }) {
		const auto name = QString::fromLatin1(kind);
		if (object.contains(name)) {
			return name;
		}
	}
	return u"unknown"_q;
}

[[nodiscard]] QString RunKey(const QString &id, const QString &theme) {
	return u"%1|%2|%3"_q.arg(id, theme, NumberText(kWidth));
}

void AddRun(ConformanceState &state, int index, const QString &theme) {
	auto &test = state.tests[index];
	ReadHtml(state, test);
	auto run = ConformanceRun{
		.id = test.id,
		.set = test.set,
		.theme = theme,
		.key = RunKey(test.id, theme),
		.notRun = !test.error.isEmpty()
			? (u"manifest entry: "_q + test.error)
			: test.htmlError,
	};
	run.message = AddWidgetFixtureMessage(*state.fixture, run.key);
	run.test = index;
	if (test.set == u"auto"_q) {
		for (const auto &stage : test.stages) {
			if (stage.themes.contains(theme)) {
				run.stages.push_back({
					.name = stage.name,
					.rect = stage.rect,
				});
			}
		}
		for (auto i = 0; i != int(test.actions.size()); ++i) {
			run.actions.push_back({
				.kind = ActionKind(test.actions[i]),
				.index = i,
			});
		}
	}
	state.runs.push_back(std::move(run));
}

// For each selected theme, light first, every selected entry in manifest
// order: an auto entry in both themes but its permanent omissions, a visual
// entry only in its own themes. A well-formed run whose message context
// this launch's chat cannot host, by its id's category, is omitted for a
// launch on a fitting chat (|elsewhere|); nothing reads the account here.
void SelectRuns(ConformanceState &state) {
	const auto &args = state.args;
	for (const auto &theme : args.themes) {
		if (!ThemeName(theme) && !state.unknownThemes.contains(theme)) {
			state.unknownThemes.push_back(theme);
		}
	}
	for (const auto &set : args.sets) {
		if (!SetName(set) && !state.unknownSets.contains(set)) {
			state.unknownSets.push_back(set);
		}
	}
	if (!state.packageError.isEmpty()) {
		return;
	}
	for (const auto &id : args.ids) {
		const auto known = std::any_of(
			begin(state.tests),
			end(state.tests),
			[&](const ManifestTest &test) { return (test.id == id); });
		if (!known && !state.unknownIds.contains(id)) {
			state.unknownIds.push_back(id);
		}
	}
	for (auto &test : state.tests) {
		test.selected = (args.sets.isEmpty() || args.sets.contains(test.set))
			&& (args.ids.isEmpty() || args.ids.contains(test.id));
		if (test.selected && !test.error.isEmpty()) {
			state.invalid.push_back(test.id + u": "_q + test.error);
		}
	}
	for (const auto &theme : { u"light"_q, u"dark"_q }) {
		if (!args.themes.isEmpty() && !args.themes.contains(theme)) {
			continue;
		}
		state.themes.push_back(theme);
		for (auto i = 0; i != int(state.tests.size()); ++i) {
			const auto &test = state.tests[i];
			const auto wanted = test.selected
				&& ((test.set == u"auto"_q)
					|| ((test.set == u"visual"_q)
						&& test.themes.contains(theme)));
			if (!wanted) {
				continue;
			}
			const auto permanent = PermanentOmission(test, theme);
			const auto partition = (permanent.isEmpty() && test.error.isEmpty())
				? ChatPartition(test, args.chat)
				: QString();
			if (permanent.isEmpty() && partition.isEmpty()) {
				AddRun(state, i, theme);
			} else {
				state.omitted.push_back({
					.id = test.id,
					.theme = theme,
					.reason = permanent.isEmpty() ? partition : permanent,
					.elsewhere = permanent.isEmpty(),
				});
			}
		}
	}
}

// ---------------------------------------------------------------------
// The result.

// The result the run file reports: the newest one, or, when a newer host
// of the run cleared it before publishing (a restart after a death), the
// last value the older host published.
[[nodiscard]] const ConformanceResult &ReportedResult(
		const ConformanceRun &run) {
	return (!run.result.present
		&& run.retained.present
		&& (run.retained.generation != run.result.generation))
		? run.retained
		: run.result;
}

// ---------------------------------------------------------------------
// Skips and the run file.

void CaptureEnvironment(ConformanceState &state) {
	auto &environment = state.environment;
	if (environment.captured) {
		return;
	}
#ifdef _DEBUG
	const auto build = u"Debug"_q;
#else // _DEBUG
	// TDESKTOP_RELEASE_TEST_HARNESS refuses to configure with LTO.
	const auto build = u"Release without LTO"_q;
#endif // _DEBUG
	environment = ConformanceEnvironment{
		.app = u"Telegram Desktop %1, %2"_q.arg(
			QString::fromLatin1(AppVersionStr),
			build),
		.os = QSysInfo::prettyProductName(),
		.cpu = QSysInfo::currentCpuArchitecture(),
		.qt = QString::fromLatin1(qVersion()),
		.locale = Tgw::UiLanguageTag(),
		.ratio = style::DevicePixelRatio(),
		.scale = style::Scale(),
		.captured = true,
	};
}

[[nodiscard]] QString EngineText(const ConformanceState &state) {
	const auto &environment = state.environment;
	auto parts = QStringList();
	if (!state.args.engine.isEmpty()) {
		parts.push_back(state.args.engine);
	}
	if (environment.captured) {
		parts.push_back(environment.app);
		parts.push_back(environment.os + u" "_q + environment.cpu);
		parts.push_back(u"Qt "_q + environment.qt);
		parts.push_back(u"TwidgetCore painted by tgw's QtBackend "
			u"(QPainter)"_q);
		parts.push_back(u"device pixel ratio %1, interface scale %2%"_q.arg(
			NumberText(environment.ratio),
			NumberText(environment.scale)));
	}
	return parts.join(u"; "_q);
}

[[nodiscard]] Matrix ComputeMatrix(const ConformanceState &state) {
	auto result = Matrix();
	for (const auto &test : state.tests) {
		if (test.set == u"auto"_q) {
			++result.autoTests;
			result.clientsAutoRuns += int(test.themes.size());
			for (const auto &theme : { u"light"_q, u"dark"_q }) {
				if (!PermanentOmission(test, theme).isEmpty()) {
					++result.permanentAuto;
				}
			}
		} else if (test.set == u"visual"_q) {
			++result.visualTests;
			result.visualManifestRuns += int(test.themes.size());
		}
	}
	for (const auto &run : state.runs) {
		const auto dark = (run.theme == u"dark"_q);
		const auto known = (run.test >= 0)
			&& (run.test < int(state.tests.size()));
		if (run.set != u"auto"_q) {
			if (dark) {
				++result.visualDark;
			} else {
				++result.visualLight;
			}
		} else if (!dark) {
			++result.autoLight;
		} else {
			++result.autoDark;
			if (known && !state.tests[run.test].themes.contains(run.theme)) {
				++result.extraDark;
			}
		}
	}
	for (const auto &omitted : state.omitted) {
		if (!omitted.elsewhere) {
			continue;
		} else if (omitted.id.startsWith(u"auto/"_q)) {
			++result.elsewhereAuto;
		} else {
			++result.elsewhereVisual;
		}
	}
	return result;
}

[[nodiscard]] QJsonObject MatrixObject(const Matrix &matrix) {
	return QJsonObject{
		{ u"autoTests"_q, matrix.autoTests },
		{ u"visualTests"_q, matrix.visualTests },
		{ u"requestedAutoRuns"_q, 2 * matrix.autoTests },
		{ u"permanentAutoOmissions"_q, matrix.permanentAuto },
		{ u"fullAutoRuns"_q, 2 * matrix.autoTests - matrix.permanentAuto },
		{ u"clientsAutoRuns"_q, matrix.clientsAutoRuns },
		{ u"visualManifestRuns"_q, matrix.visualManifestRuns },
		{ u"autoLight"_q, matrix.autoLight },
		{ u"autoDark"_q, matrix.autoDark },
		{ u"autoRuns"_q, matrix.autoLight + matrix.autoDark },
		{ u"extraDark"_q, matrix.extraDark },
		{ u"visualLight"_q, matrix.visualLight },
		{ u"visualDark"_q, matrix.visualDark },
		{ u"elsewhereAuto"_q, matrix.elsewhereAuto },
		{ u"elsewhereVisual"_q, matrix.elsewhereVisual },
	};
}

// The reconciliation the run file states, computed from the manifest and
// the selection: the requested auto matrix, each omission and whether it
// departs from that matrix, this launch's share of it, the dark runs
// beyond the manifest's own themes, the visual load checks, the stop wait,
// the caption and the host watch, the message contexts, the requested
// pictures and a narrowed selection.
[[nodiscard]] QStringList Notes(
		const ConformanceState &state,
		const Matrix &matrix) {
	const auto &args = state.args;
	const auto requested = 2 * matrix.autoTests;
	const auto full = requested - matrix.permanentAuto;
	auto result = QStringList();
	result.push_back(u"auto: %1 light + %2 dark = %3 runs, each at %4 CSS "
		u"px. The requested matrix is every auto test of the manifest in "
		u"both themes: %5 x 2 = %6 runs."_q.arg(
			NumberText(matrix.autoLight),
			NumberText(matrix.autoDark),
			NumberText(matrix.autoLight + matrix.autoDark),
			NumberText(kWidth),
			NumberText(matrix.autoTests),
			NumberText(requested)));
	for (const auto &omitted : state.omitted) {
		result.push_back(omitted.elsewhere
			? u"%1 in %2 is not run in this launch: %3. This is no departure "
				u"from the matrix: omittedRuns marks it elsewhere, for a "
				u"launch on a fitting chat."_q.arg(
					omitted.id,
					omitted.theme,
					omitted.reason)
			: u"%1 in %2 is omitted: %3. This departs from the requested %4 "
				u"x 2 matrix: with every other auto run of the manifest, a run "
				u"file has %5 auto runs, not %6."_q.arg(
					omitted.id,
					omitted.theme,
					omitted.reason,
					NumberText(matrix.autoTests),
					NumberText(full),
					NumberText(requested)));
	}
	const auto permanentHere = std::count_if(
		begin(state.omitted),
		end(state.omitted),
		[](const OmittedRun &omitted) { return !omitted.elsewhere; });
	result.push_back(u"this launch, for its selection only: %1 auto runs and "
		u"%2 of the manifest's %3 permanent auto omissions; %4 auto and %5 "
		u"visual runs left to a launch on another chat, which subtract "
		u"nothing from the matrix. The full auto matrix of %6 runs (%7 "
		u"requested less the permanent omissions) comes only from every "
		u"launch's runs together."_q.arg(
			NumberText(matrix.autoLight + matrix.autoDark),
			NumberText(permanentHere),
			NumberText(matrix.permanentAuto),
			NumberText(matrix.elsewhereAuto),
			NumberText(matrix.elsewhereVisual),
			NumberText(full),
			NumberText(requested)));
	if (matrix.extraDark > 0) {
		result.push_back(u"%1 of the dark auto runs are of tests whose "
			u"manifest themes do not include dark. They go beyond "
			u"CLIENTS.md's loop over each test's themes, which gives %2 "
			u"auto runs for the manifest; report.mjs counts their checks "
			u"and skips their stages by each stage's own themes."_q.arg(
				NumberText(matrix.extraDark),
				NumberText(matrix.clientsAutoRuns)));
	}
	if (matrix.visualLight || matrix.visualDark) {
		result.push_back(u"visual: %1 light + %2 dark = %3 runs, each only "
			u"in its manifest themes (%4 for the manifest), as a load "
			u"check: no actions and no crops."_q.arg(
				NumberText(matrix.visualLight),
				NumberText(matrix.visualDark),
				NumberText(matrix.visualLight + matrix.visualDark),
				NumberText(matrix.visualManifestRuns)));
	}
	result.push_back(u"observed.bridge holds the calls each run's host "
		u"accepted after its gesture gate, and errors its console's "
		u"\"Uncaught \" errors, both read from the runtime's raw summary of "
		u"the run's only host, whose formatter cuts each text at 240 "
		u"characters and keeps a console tail of at most 24 KB; a run whose "
		u"summary does not establish them carries a named runner refusal "
		u"and leaves them out. As report.mjs would count a missing "
		u"observed.bridge as no calls, such a run also carries host: every "
		u"host check of its manifest entry judged as report.mjs judges it, "
		u"each one counting bridge calls skipped as not observed."_q);
	result.push_back(u"each stage crop is cut at its manifest rect from one "
		u"snapshot of the union of the run's stage rects, the chat list's "
		u"own paint of the card at %1 image px per CSS px; cards/ holds "
		u"each snapshot."_q.arg(NumberText(kCropScale)));
	result.push_back(u"an entry with stops waits for its widget's death, "
		u"never for done, as CLIENTS.md's step 4 asks: its result wait ends "
		u"only at a fatal frozen notice, a stopped or failed runtime, the "
		u"message gone or the %1 s bound. Its dead is the reason of the "
		u"\"Died: \" line of its host's final or fresh runtime summary, "
		u"WidgetDiagnostics::dead verbatim but cut at 240 characters, or, "
		u"for a stop the desktop decided itself (a paint bound), which no "
		u"host death carries, RendererCore's stop reason cut the same way, "
		u"read in the log stage; a stop run that died with neither carries "
		u"the runner refusal stop-reason-unmeasured. report.mjs alone judges "
		u"whether dead starts with the entry's stops."_q.arg(
			NumberText(kResultBound / 1000)));
	auto uncaptioned = QStringList();
	for (const auto &test : state.tests) {
		if (test.safeArea) {
			uncaptioned.push_back(test.id);
		}
	}
	result.push_back(u"every run's message but those of the manifest's "
		u"safeArea entries ([%1]) has its manifest id as its text, the "
		u"caption below the widget, so the desktop itself gives its host "
		u"CLIENTS.md's all-0 content safe area and draws the time in the "
		u"caption's line. A safeArea entry's message has no caption: its host "
		u"gets the band and corner the desktop passes for a widget with no "
		u"caption below it, with the time drawn over that corner. "
		u"safeArea.applied is the content safe area each construction of the "
		u"run's host got, and safeArea.caption the text (null for none); the "
		u"manifest's safeArea numbers are its reference picture's, not "
		u"expectations. A run that is no safeArea entry and got a nonzero area "
		u"carries safe-area-not-zero, and a loaded run without a reading "
		u"safe-area-unmeasured. Every run's host is watched with an empty "
		u"override, which changes no option; with the watch's onChange set, "
		u"the host copies its whole diagnostics on its host thread for every "
		u"change, host-thread work beside the widget's, neither JS nor "
		u"main-thread work."_q.arg(
			uncaptioned.join(u", "_q)));
	result.append(ContextNotes(state));
	result.append(PictureNotes(state));
	if (!args.themes.isEmpty() || !args.sets.isEmpty() || !args.ids.isEmpty()) {
		result.push_back(u"this launch ran a selection (themes [%1], sets "
			u"[%2], ids [%3]): runs outside it are not in this file."_q.arg(
				args.themes.join(u","_q),
				args.sets.join(u","_q),
				args.ids.join(u","_q)));
	}
	return result;
}

[[nodiscard]] QJsonObject ResultObject(const ConformanceResult &result) {
	return QJsonObject{
		{ u"present"_q, result.present },
		{ u"error"_q, result.error },
		{ u"generation"_q, result.generation },
		{ u"serial"_q, result.serial },
		{ u"updates"_q, result.updates },
		{ u"htmlId"_q, qint64(result.htmlId) },
	};
}

[[nodiscard]] QJsonObject TapObject(
		const WidgetOpsReading &r,
		bool finished) {
	return QJsonObject{
		{ u"final"_q, finished },
		{ u"factoriesSelected"_q, r.factoriesSelected },
		{ u"factoriesReleased"_q, r.factoriesReleased },
		{ u"wrappersBuilt"_q, r.wrappersBuilt },
		{ u"wrappersDestroyed"_q, r.wrappersDestroyed },
		{ u"batches"_q, r.batches },
		{ u"ops"_q, r.ops },
		{ u"scanGaps"_q, r.scanGaps },
		{ u"gapExamples"_q, Strings(r.gapExamples) },
		{ u"leaks"_q, r.leaks },
		{ u"leakExamples"_q, Strings(r.leakExamples) },
		{ u"notes"_q, r.notes },
		{ u"violations"_q, r.violations },
		{ u"ackQueries"_q, r.ackQueries },
		{ u"ackResults"_q, r.ackResults },
		{ u"fatalFrozen"_q, r.fatalFrozen },
	};
}

[[nodiscard]] QString SummaryStateName(WidgetLogSummary state) {
	switch (state) {
	case WidgetLogSummary::Absent: return u"absent"_q;
	case WidgetLogSummary::Fresh: return u"fresh"_q;
	case WidgetLogSummary::Final: return u"final"_q;
	case WidgetLogSummary::Stale: return u"stale"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QJsonObject ViewObject(const ViewReading &view) {
	return QJsonObject{
		{ u"read"_q, view.read },
		{ u"list"_q, view.list },
		{ u"loadView"_q, view.loadView },
		{ u"view"_q, view.view },
		{ u"viewChanged"_q, view.read && (view.loadView != view.view) },
		{ u"core"_q, view.core },
		{ u"state"_q, view.state },
		{ u"card"_q, RectArray(view.card) },
		{ u"coreWidth"_q, OptionalNumber(view.width) },
		{ u"scheme"_q, view.scheme },
		{ u"schemeChanges"_q, view.schemeChanges },
		{ u"generation"_q, view.generation },
		{ u"themeMessage"_q, view.themeMessage },
	};
}

[[nodiscard]] QJsonObject LoadObject(const ConformanceRun &run) {
	auto result = QJsonObject{
		{ u"show"_q, ShowObject(run.loadShow.get()) },
		{ u"outcome"_q, run.resultOutcome },
		{ u"waitMs"_q, qint64(run.resultMs) },
		{ u"runtimeState"_q, run.runtimeState },
	};
	if (!run.actions.empty()) {
		result.insert(u"afterActions"_q, QJsonObject{
			{ u"outcome"_q, run.doneOutcome },
			{ u"waitMs"_q, qint64(run.doneMs) },
		});
	}
	return result;
}

[[nodiscard]] QJsonObject SnapshotObject(const ConformanceRun &run) {
	const auto &snapshot = run.snapshot;
	return QJsonObject{
		{ u"css"_q, RectArray(snapshot.css) },
		{ u"list"_q, RectArray(snapshot.list) },
		{ u"card"_q, RectArray(run.snapshotCard.card) },
		{
			u"image"_q,
			QJsonArray{ snapshot.image.width(), snapshot.image.height() },
		},
		{ u"devicePixelRatio"_q, snapshot.ratio },
		{ u"interfaceScale"_q, snapshot.scale },
		{ u"file"_q, snapshot.file },
		{ u"refusal"_q, snapshot.refusal },
		{ u"detail"_q, snapshot.detail },
		{ u"show"_q, ShowObject(run.snapshotShow.get()) },
		{ u"dateWait"_q, DateWaitObject(run.snapshotDate.get()) },
	};
}

[[nodiscard]] QJsonObject LogObject(const ConformanceRun &run) {
	if (!run.message) {
		return QJsonObject{ { u"source"_q, QString() } };
	}
	const auto &log = run.message->log;
	return QJsonObject{
		{ u"source"_q, log.source },
		{ u"summaryState"_q, SummaryStateName(log.summaryState) },
		{ u"pending"_q, log.pending },
		{ u"received"_q, log.received },
		{ u"chars"_q, log.chars },
		{ u"summaryChars"_q, int(log.summary.size()) },
		{ u"deathLine"_q, run.deathLine },
		{ u"summary"_q, run.summaryRead
			? QJsonValue(ConformanceSummaryObject(run.summary))
			: QJsonValue(QJsonValue::Null) },
	};
}

// What the run's hosts were observed doing; the refusals are the
// summary's own, none when it was never read.
[[nodiscard]] ConformanceObservation ObservationOf(const ConformanceRun &run) {
	return {
		.bridge = run.bridge,
		.bridgeRefusals = run.summaryRead
			? ConformanceBridgeRefusals(run.summary)
			: QStringList(),
		.height = run.height,
		.chatFocused = run.chatFocused,
		.focusHolder = run.focusHolder,
	};
}

[[nodiscard]] QJsonArray ManifestHost(
		const ConformanceState &state,
		const ConformanceRun &run) {
	return ((run.test >= 0) && (run.test < int(state.tests.size())))
		? state.tests[run.test].host
		: QJsonArray();
}

// A completed run: the schema's fields, then the documented extras. An
// observation that was not made is left out, never filled in: unknown
// bridge calls come with every host check judged instead, as
// ConformanceHostFieldsOf() says. A stage without a crop carries only its
// refusal, so report.mjs skips it as having no crop rather than taking a
// judgement from it.
[[nodiscard]] QJsonObject RunObject(
		const ConformanceState &state,
		const ConformanceRun &run) {
	auto stages = QJsonArray();
	for (const auto &stage : run.stages) {
		auto entry = QJsonObject{
			{ u"name"_q, stage.name },
			{ u"rect"_q, RectArray(stage.rect) },
		};
		if (!stage.crop.isEmpty()) {
			entry.insert(u"crop"_q, stage.crop);
		} else {
			entry.insert(u"detail"_q, stage.refusal);
		}
		if (!stage.capture.isEmpty()) {
			entry.insert(u"capture"_q, stage.capture);
		}
		stages.push_back(entry);
	}
	const auto host = ConformanceHostFieldsOf(
		ObservationOf(run),
		ManifestHost(state, run));
	auto skipped = QJsonArray();
	auto reasons = QJsonArray();
	auto actions = QJsonArray();
	for (const auto &action : run.actions) {
		if (!action.skipReason.isEmpty()) {
			skipped.push_back(action.index);
			reasons.push_back(QJsonObject{
				{ u"index"_q, action.index },
				{ u"kind"_q, action.kind },
				{ u"reason"_q, action.skipReason },
			});
		}
		actions.push_back(QJsonObject{
			{ u"index"_q, action.index },
			{ u"kind"_q, action.kind },
			{ u"performed"_q, action.performed },
			{ u"refusal"_q, action.refusal },
			{ u"skipReason"_q, action.skipReason },
			{ u"reading"_q, action.reading },
		});
	}
	auto refusals = QJsonArray();
	for (const auto &refusal : run.refusals) {
		refusals.push_back(QJsonObject{
			{ u"name"_q, refusal.name },
			{ u"detail"_q, refusal.detail },
		});
	}
	const auto dead = !run.died
		? QJsonValue(QJsonValue::Null)
		: run.dead.isEmpty()
		? QJsonValue(u"died, reason not observed"_q)
		: QJsonValue(run.dead);
	const auto &reported = ReportedResult(run);
	auto resultObject = ResultObject(reported);
	resultObject.insert(u"retained"_q, (&reported != &run.result));
	auto result = QJsonObject{
		{ u"id"_q, run.id },
		{ u"theme"_q, run.theme },
		{ u"width"_q, (run.width > 0) ? run.width : kWidth },
		{ u"loaded"_q, run.loaded },
		{ u"dead"_q, dead },
		{ u"checks"_q, reported.present ? reported.checks : QJsonArray() },
		{ u"stages"_q, stages },
		{ u"observed"_q, host.observed },
		{ u"skippedActions"_q, skipped },
		{ u"key"_q, run.key },
		{ u"set"_q, run.set },
		{ u"widthMeasured"_q, (run.width > 0) },
		{ u"skippedReasons"_q, reasons },
		{ u"actions"_q, actions },
		{ u"runnerRefusals"_q, refusals },
		{ u"result"_q, resultObject },
		{ u"tap"_q, TapObject(run.reading, run.readingFinal) },
		{ u"focus"_q, ComposeObject(run.compose) },
		{ u"view"_q, ViewObject(run.view) },
		{ u"load"_q, LoadObject(run) },
		{ u"log"_q, LogObject(run) },
		{ u"contentHeight"_q, OptionalNumber(run.contentHeight) },
		{ u"layers"_q, run.message
			? Strings(run.message->layers)
			: QJsonArray() },
	};
	if (reported.present) {
		result.insert(u"status"_q, reported.status);
	}
	if (host.host) {
		result.insert(u"host"_q, *host.host);
	}
	if (run.errors) {
		result.insert(u"errors"_q, Strings(*run.errors));
	}
	if (!run.stages.empty()) {
		result.insert(u"snapshot"_q, SnapshotObject(run));
	}
	if (run.safeAreaRead) {
		result.insert(u"safeArea"_q, SafeAreaObject(run));
	}
	InsertContextExtras(result, state, run);
	InsertPictureExtra(result, run);
	return result;
}

// A selected run that did not complete, with the state it reached.
[[nodiscard]] QJsonObject IncompleteObject(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto skip = RunSkip(state, run);
	const auto &reported = ReportedResult(run);
	auto result = QJsonObject{
		{ u"id"_q, run.id },
		{ u"theme"_q, run.theme },
		{ u"width"_q, kWidth },
		{ u"key"_q, run.key },
		{ u"state"_q, run.progress },
		{ u"reason"_q, skip.isEmpty()
			? u"the launch ended before this run completed"_q
			: skip },
		{ u"lastStatus"_q, reported.status },
		{ u"result"_q, ResultObject(reported) },
	};
	if (run.message && run.message->tap) {
		result.insert(u"tap"_q, TapObject(run.reading, run.readingFinal));
	}
	InsertContextExtras(result, state, run);
	InsertPictureExtra(result, run);
	return result;
}

[[nodiscard]] QJsonArray OmittedArray(const ConformanceState &state) {
	auto result = QJsonArray();
	for (const auto &omitted : state.omitted) {
		result.push_back(QJsonObject{
			{ u"id"_q, omitted.id },
			{ u"theme"_q, omitted.theme },
			{ u"width"_q, kWidth },
			{ u"reason"_q, omitted.reason },
			{ u"elsewhere"_q, omitted.elsewhere },
		});
	}
	return result;
}

[[nodiscard]] QJsonObject EnvironmentObject(
		const ConformanceEnvironment &environment) {
	return QJsonObject{
		{ u"captured"_q, environment.captured },
		{ u"app"_q, environment.app },
		{ u"os"_q, environment.os },
		{ u"cpu"_q, environment.cpu },
		{ u"qt"_q, environment.qt },
		{ u"locale"_q, environment.locale },
		{ u"devicePixelRatio"_q, environment.ratio },
		{ u"interfaceScale"_q, environment.scale },
	};
}

[[nodiscard]] QJsonObject SelectionObject(const ConformanceState &state) {
	return QJsonObject{
		{ u"themes"_q, Strings(state.args.themes) },
		{ u"sets"_q, Strings(state.args.sets) },
		{ u"ids"_q, Strings(state.args.ids) },
		{ u"pictures"_q, Strings(state.args.pictures) },
		{ u"runThemes"_q, Strings(state.themes) },
		{ u"unknownThemes"_q, Strings(state.unknownThemes) },
		{ u"unknownSets"_q, Strings(state.unknownSets) },
		{ u"unknownIds"_q, Strings(state.unknownIds) },
		{ u"unknownPictures"_q, Strings(state.unknownPictures) },
	};
}

[[nodiscard]] QJsonObject SelfCheckObject(const ConformanceState &state) {
	return QJsonObject{
		{ u"ran"_q, state.selfChecked },
		{ u"rows"_q, state.selfCheckRows },
		{ u"failed"_q, Strings(state.selfCheckFailed) },
	};
}

[[nodiscard]] QJsonArray PaletteArray(const ConformanceState &state) {
	auto result = QJsonArray();
	for (const auto &phase : state.palette) {
		result.push_back(QJsonObject{
			{ u"theme"_q, phase.theme },
			{ u"restore"_q, phase.restore
				? PaletteRestoreText(*phase.restore)
				: QString() },
			{ u"switch"_q, PaletteSwitchText(*phase.switched) },
		});
	}
	return result;
}

[[nodiscard]] QJsonObject OutgoingObject(const OutgoingItem &item) {
	return QJsonObject{
		{ u"run"_q, item.run },
		{ u"id"_q, item.id },
		{ u"wallMs"_q, item.wallMs },
		{ u"inChat"_q, item.inChat },
		{ u"local"_q, item.local },
		{ u"regular"_q, item.regular },
		{ u"service"_q, item.service },
	};
}

[[nodiscard]] QJsonArray OutgoingArray(const ConformanceState &state) {
	auto result = QJsonArray();
	for (const auto &item : state.outgoing) {
		result.push_back(OutgoingObject(item));
	}
	return result;
}

[[nodiscard]] QJsonObject RunFileObject(
		const ConformanceState &state,
		const QString &writtenBy) {
	const auto matrix = ComputeMatrix(state);
	auto runs = QJsonArray();
	auto incomplete = QJsonArray();
	for (const auto &run : state.runs) {
		if (run.completed) {
			runs.push_back(RunObject(state, run));
		} else {
			incomplete.push_back(IncompleteObject(state, run));
		}
	}
	// The client's names: report.mjs writes desktop-twidget-core.json.
	return QJsonObject{
		{ u"platform"_q, u"desktop"_q },
		{ u"renderer"_q, u"twidget-core"_q },
		{ u"engine"_q, EngineText(state) },
		{ u"framework"_q, state.args.framework },
		{ u"policy"_q, QString::fromLatin1(kPolicy) },
		{ u"features"_q, QJsonArray{ QString::fromLatin1(kFeature) } },
		{ u"date"_q, state.date },
		{ u"runs"_q, runs },
		{ u"locale"_q, state.environment.locale },
		{ u"environment"_q, EnvironmentObject(state.environment) },
		{ u"notes"_q, Strings(Notes(state, matrix)) },
		{ u"omittedRuns"_q, OmittedArray(state) },
		{ u"incompleteRuns"_q, incomplete },
		{ u"matrix"_q, MatrixObject(matrix) },
		{ u"selection"_q, SelectionObject(state) },
		{ u"selfCheck"_q, SelfCheckObject(state) },
		{ u"palette"_q, PaletteArray(state) },
		{ u"paletteRestore"_q, state.paletteRestore
			? PaletteRestoreText(*state.paletteRestore)
			: QString() },
		{ u"unexpectedOutgoing"_q, OutgoingArray(state) },
		{ u"writtenBy"_q, writtenBy },
	};
}

// The output directory must be absent or empty, so nothing of an earlier
// launch is overwritten or taken for this one's.
void PrepareOutput(ConformanceState &state) {
	const auto &output = state.args.output;
	const auto dir = output.isEmpty()
		? (EvidenceDir() + u"conformance"_q)
		: output;
	const auto info = QFileInfo(dir);
	state.outputDir = QDir::cleanPath(dir);
	state.runFile = QDir(state.outputDir).filePath(u"run.json"_q);
	const auto entries = QDir::AllEntries
		| QDir::NoDotAndDotDot
		| QDir::Hidden
		| QDir::System;
	if (!info.isAbsolute()) {
		state.outputError = u"the output directory [%1] is not an absolute "
			u"path"_q.arg(dir);
	} else if (info.exists() && !info.isDir()) {
		state.outputError = u"the output path [%1] is not a "
			u"directory"_q.arg(dir);
	} else if (info.exists() && !QDir(dir).isEmpty(entries)) {
		state.outputError = u"the output directory [%1] is not fresh: it "
			u"already holds files"_q.arg(dir);
	} else if (!QDir().mkpath(dir)) {
		state.outputError = u"the output directory [%1] could not be "
			u"created"_q.arg(dir);
	} else {
		state.outputReady = true;
	}
}

// Writes the run file atomically, replacing this launch's earlier write;
// returns whether it is on disk now.
[[nodiscard]] bool SaveRunFile(
		ConformanceState &state,
		const QString &writtenBy) {
	CaptureEnvironment(state);
	for (auto &run : state.runs) {
		if (!run.completed) {
			RefreshResult(run);
		}
	}
	if (!state.outputReady) {
		state.saveError = state.outputError.isEmpty()
			? u"the output directory was never prepared"_q
			: state.outputError;
		return false;
	}
	const auto bytes = QJsonDocument(
		RunFileObject(state, writtenBy)).toJson(QJsonDocument::Indented);
	auto file = QSaveFile(state.runFile);
	auto error = QString();
	if (!file.open(QIODevice::WriteOnly)) {
		error = u"open: "_q + file.errorString();
	} else if (file.write(bytes) != bytes.size()) {
		error = u"write: "_q + file.errorString();
		file.cancelWriting();
	} else if (!file.commit()) {
		error = u"commit: "_q + file.errorString();
	}
	state.saveError = error;
	if (!error.isEmpty()) {
		return false;
	}
	++state.writes;
	state.writtenBy = writtenBy;
	state.writtenBytes = bytes.size();
	return true;
}

// ---------------------------------------------------------------------
// Self-checks.

// Pure UpdateResult() controls against literal readings.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> CheckResultReader() {
	const auto id = u"auto/x"_q;
	auto latest = WidgetOpsLatest();
	latest.generation = 1;
	latest.serial = 1;
	latest.updates = 1;
	latest.adopted = true;
	latest.htmlId = 2;
	latest.conformance = std::string(R"({"v":1,"id":"auto/x",)"
		R"("status":"awaiting","n":{"pass":1,"pending":1},"checks":)"
		R"([["css.display.flex","row","pass",""],)"
		R"(["input.tap","tap","pending",""]],)"
		R"("targets":{"btn":[10,20,64,28]}})");
	const auto expected = QJsonArray{
		QJsonArray{ u"css.display.flex"_q, u"row"_q, u"pass"_q, QString() },
		QJsonArray{ u"input.tap"_q, u"tap"_q, u"pending"_q, QString() },
	};
	auto result = ConformanceResult();
	const auto taken = UpdateResult(result, latest, id);
	const auto accepted = taken
		&& result.present
		&& result.error.isEmpty()
		&& (result.status == u"awaiting"_q)
		&& (result.checks == expected)
		&& (result.targets.value(u"btn"_q).toArray()
			== QJsonArray{ 10, 20, 64, 28 })
		&& (result.htmlId == 2);
	const auto cached = !UpdateResult(result, latest, id) && result.present;
	auto rows = std::vector<WidgetOpsSelfCheckRow>();
	rows.push_back(SelfCheckRow(
		accepted && cached,
		u"result reader self-check: this run's value is taken with its "
			u"checks verbatim, once per generation and serial"_q,
		u"taken=%1 present=%2 status=%3 checks=%4 cached=%5"_q.arg(
			NumberText(taken ? 1 : 0),
			NumberText(result.present ? 1 : 0),
			result.status,
			NumberText(result.checks.size()),
			NumberText(cached ? 1 : 0))));

	auto readings = QStringList();
	const auto refuse = [&](int serial, std::optional<std::string> value) {
		auto next = latest;
		next.serial = serial;
		next.conformance = std::move(value);
		UpdateResult(result, next, id);
		readings.push_back(u"{present=%1 status=[%2] error=[%3]}"_q.arg(
			NumberText(result.present ? 1 : 0),
			result.status,
			result.error));
		return !result.present && result.status.isEmpty();
	};
	const auto foreign = refuse(
		2,
		R"({"v":1,"id":"auto/y","status":"done","checks":[]})")
		&& result.error.contains(u"auto/y"_q);
	const auto unparsable = refuse(3, R"({"v":1,)")
		&& !result.error.isEmpty();
	const auto checksless = refuse(
		4,
		R"({"v":1,"id":"auto/x","status":"done","checks":{}})")
		&& !result.error.isEmpty();
	auto cleared = latest;
	cleared.generation = 2;
	cleared.serial = 5;
	cleared.conformance = std::nullopt;
	UpdateResult(result, cleared, id);
	const auto clear = !result.present
		&& result.error.isEmpty()
		&& result.status.isEmpty()
		&& (result.generation == 2);
	readings.push_back(u"{cleared present=%1 generation=%2}"_q.arg(
		NumberText(result.present ? 1 : 0),
		NumberText(result.generation)));
	rows.push_back(SelfCheckRow(
		foreign && unparsable && checksless && clear,
		u"result reader self-check: another run's id, text that is no "
			u"JSON object and checks that are no array are refused by "
			u"name, and a newer host's clear leaves no value"_q,
		readings.join(QChar(' '))));
	return rows;
}

// Pure RunFileObject() controls: a completed run carries the schema's
// fields; a run that did not complete is never written as one.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> CheckRunFileModel() {
	auto control = ConformanceState();
	control.args.framework = u"v0.0.0"_q;
	const auto checks = QJsonArray{
		QJsonArray{ u"input.tap"_q, u"tap"_q, u"pass"_q, QString() },
	};
	auto done = ConformanceRun{
		.id = u"auto/x"_q,
		.set = u"auto"_q,
		.theme = u"light"_q,
		.key = RunKey(u"auto/x"_q, u"light"_q),
	};
	done.result.present = true;
	done.result.status = u"done"_q;
	done.result.checks = checks;
	done.stages.push_back({
		.name = u"cropped"_q,
		.rect = QRect(8, 8, 80, 60),
		.crop = u"crops/x.png"_q,
		.refusal = QString(),
	});
	done.stages.push_back({
		.name = u"refused"_q,
		.rect = QRect(96, 8, 80, 60),
		.refusal = u"not-2x"_q,
	});
	done.actions.push_back({
		.kind = u"tap"_q,
		.refusal = QString(),
		.index = 0,
		.performed = true,
	});
	done.actions.push_back({
		.kind = u"key"_q,
		.refusal = QString(),
		.skipReason = u"a control reason"_q,
		.index = 1,
	});
	done.actions.push_back({
		.kind = u"drag"_q,
		.index = 2,
	});
	done.bridge = std::vector<BridgeCall>{
		{
			.method = u"openLink"_q,
			.args = QJsonArray{ u"https://example.org/x"_q },
		},
		{
			.method = u"haptic"_q,
			.args = QJsonArray{ u"impact"_q, u"light"_q },
		},
	};
	done.height = 120.;
	done.chatFocused = true;
	done.width = kWidth;
	done.loaded = true;
	done.completed = true;
	auto cut = ConformanceRun{
		.id = u"auto/y"_q,
		.set = u"auto"_q,
		.theme = u"dark"_q,
		.key = RunKey(u"auto/y"_q, u"dark"_q),
	};
	cut.progress = u"injected"_q;
	control.runs.push_back(std::move(done));
	control.runs.push_back(std::move(cut));
	control.omitted.push_back({
		.id = u"auto/z"_q,
		.theme = u"dark"_q,
		.reason = u"a control reason"_q,
	});
	control.omitted.push_back({
		.id = u"auto/w"_q,
		.theme = u"light"_q,
		.reason = u"a control chat reason"_q,
		.elsewhere = true,
	});

	const auto file = RunFileObject(control, u"self-check"_q);
	const auto runs = file.value(u"runs"_q).toArray();
	const auto entry = runs.isEmpty()
		? QJsonObject()
		: runs.first().toObject();
	const auto observed = entry.value(u"observed"_q).toObject();
	const auto calls = observed.value(u"bridge"_q).toArray();
	auto arrays = (calls.size() == 2);
	for (const auto &call : calls) {
		arrays = arrays && call.toObject().value(u"args"_q).isArray();
	}
	const auto stages = entry.value(u"stages"_q).toArray();
	const auto staged = (stages.size() == 2);
	const auto cropped = staged ? stages[0].toObject() : QJsonObject();
	const auto refused = staged ? stages[1].toObject() : QJsonObject();
	const auto modelled = (runs.size() == 1)
		&& (entry.value(u"id"_q).toString() == u"auto/x"_q)
		&& entry.value(u"theme"_q).isString()
		&& entry.value(u"width"_q).isDouble()
		&& entry.value(u"loaded"_q).isBool()
		&& entry.value(u"dead"_q).isNull()
		&& (entry.value(u"checks"_q).toArray() == checks)
		&& (entry.value(u"status"_q).toString() == u"done"_q)
		&& arrays
		&& (observed.value(u"height"_q).toDouble() == 120.)
		&& (observed.value(u"chatFocused"_q) == QJsonValue(true))
		&& (observed.value(u"syntheticInput"_q) == QJsonValue(false))
		&& staged
		&& (cropped.value(u"crop"_q).toString() == u"crops/x.png"_q)
		&& !refused.contains(u"crop"_q)
		&& !refused.contains(u"status"_q)
		&& !refused.contains(u"diff"_q)
		&& (refused.value(u"detail"_q).toString() == u"not-2x"_q)
		&& (entry.value(u"skippedActions"_q).toArray() == QJsonArray{ 1 });
	auto rows = std::vector<WidgetOpsSelfCheckRow>();
	rows.push_back(SelfCheckRow(
		modelled,
		u"run file self-check: a completed run has the schema's fields, "
			u"its checks verbatim, its bridge args as arrays, a stage "
			u"without a crop carrying no crop or judgement, and only a true "
			u"skip in skippedActions, never an action left unperformed"_q,
		CompactJson(entry)));

	const auto incomplete = file.value(u"incompleteRuns"_q).toArray();
	const auto cutEntry = incomplete.isEmpty()
		? QJsonObject()
		: incomplete.first().toObject();
	const auto notes = file.value(u"notes"_q).toArray();
	auto noted = false;
	auto notedElsewhere = false;
	for (const auto &note : notes) {
		const auto text = note.toString();
		noted = noted || text.startsWith(u"auto/z in dark is omitted: "_q);
		notedElsewhere = notedElsewhere
			|| text.startsWith(
				u"auto/w in light is not run in this launch: "_q);
	}
	const auto omitted = file.value(u"omittedRuns"_q).toArray();
	const auto omission = [&](int index) {
		return omitted.at(index).toObject();
	};
	const auto separated = (incomplete.size() == 1)
		&& (cutEntry.value(u"key"_q).toString()
			== RunKey(u"auto/y"_q, u"dark"_q))
		&& (cutEntry.value(u"state"_q).toString() == u"injected"_q)
		&& !cutEntry.value(u"reason"_q).toString().isEmpty()
		&& (omitted.size() == 2)
		&& (omission(0).value(u"elsewhere"_q) == QJsonValue(false))
		&& (omission(1).value(u"elsewhere"_q) == QJsonValue(true))
		&& noted
		&& notedElsewhere
		&& (file.value(u"platform"_q).toString() == u"desktop"_q)
		&& (file.value(u"renderer"_q).toString() == u"twidget-core"_q)
		&& (file.value(u"policy"_q).toString() == u"strict"_q)
		&& (file.value(u"features"_q).toArray()
			== QJsonArray{ u"webAnimations"_q });
	rows.push_back(SelfCheckRow(
		separated,
		u"run file self-check: a run that did not complete is only named "
			u"in incompleteRuns, never written into runs, and an omission "
			u"is stated in omittedRuns and the notes, elsewhere true only "
			u"for one left to a launch on another chat"_q,
		CompactJson(QJsonObject{
			{ u"incompleteRuns"_q, incomplete },
			{ u"omittedRuns"_q, file.value(u"omittedRuns"_q) },
			{ u"notes"_q, notes },
		})));
	return rows;
}

// RunFileObject() of two completed runs of one manifest entry with host
// checks: one whose bridge reading was refused, one with a reading of no
// call.
[[nodiscard]] WidgetOpsSelfCheckRow CheckRunFileHost() {
	auto control = ConformanceState();
	control.args.framework = u"v0.0.0"_q;
	control.tests.push_back(ManifestTest{
		.id = u"auto/x"_q,
		.set = u"auto"_q,
		.host = QJsonArray{
			QJsonObject{
				{ u"id"_q, u"input.key-gesture"_q },
				{ u"name"_q, u"a key press does not unlock openLink"_q },
				{ u"bridge"_q, QJsonObject{
					{ u"method"_q, u"openLink"_q },
					{ u"arg"_q, u"https://example.org/conformance/key"_q },
					{ u"count"_q, 0 },
				} },
				{ u"gesture"_q, true },
			},
			QJsonObject{
				{ u"id"_q, u"js.focus"_q },
				{ u"name"_q, u"focus() leaves the chat input focused"_q },
				{ u"focus"_q, u"chat"_q },
			},
		},
	});
	const auto completed = [](const QString &theme) {
		auto result = ConformanceRun{
			.id = u"auto/x"_q,
			.set = u"auto"_q,
			.theme = theme,
			.key = RunKey(u"auto/x"_q, theme),
		};
		result.test = 0;
		result.chatFocused = true;
		result.loaded = true;
		result.completed = true;
		result.summaryRead = true;
		return result;
	};
	auto measured = completed(u"light"_q);
	measured.bridge = std::vector<BridgeCall>();
	auto refused = completed(u"dark"_q);
	refused.summary.refusals.push_back({
		.name = u"summary-multiple-hosts"_q,
		.detail = u"a control detail"_q,
	});
	refused.refusals = refused.summary.refusals;
	control.runs.push_back(std::move(measured));
	control.runs.push_back(std::move(refused));
	const auto runs = RunFileObject(
		control,
		u"self-check"_q).value(u"runs"_q).toArray();
	const auto first = runs.at(0).toObject();
	const auto second = runs.at(1).toObject();
	const auto host = second.value(u"host"_q).toArray();
	const auto key = host.at(0).toObject();
	const auto focus = host.at(1).toObject();
	const auto observed = [](const QJsonObject &entry) {
		return entry.value(u"observed"_q).toObject();
	};
	const auto passed = (runs.size() == 2)
		&& !first.contains(u"host"_q)
		&& (observed(first).value(u"bridge"_q) == QJsonValue(QJsonArray()))
		&& !observed(second).contains(u"bridge"_q)
		&& (observed(second).value(u"chatFocused"_q) == QJsonValue(true))
		&& (host.size() == 2)
		&& (key.value(u"id"_q).toString() == u"input.key-gesture"_q)
		&& (key.value(u"status"_q).toString() == u"skip"_q)
		&& key.value(u"detail"_q).toString().contains(
			u"summary-multiple-hosts"_q)
		&& (focus.value(u"id"_q).toString() == u"js.focus"_q)
		&& (focus.value(u"status"_q).toString() == u"pass"_q)
		&& (focus.value(u"detail"_q).toString()
			== u"chat input kept the focus"_q);
	return SelfCheckRow(
		passed,
		u"run file self-check: a completed run whose bridge reading was "
			u"refused carries every host check of its manifest entry judged, "
			u"the one counting bridge calls skipped naming the refusal, and "
			u"no observed.bridge; a run whose reading has no call carries "
			u"that and no host"_q,
		CompactJson(QJsonObject{ { u"runs"_q, runs } }));
}

// SelectRuns() of literal entries in a user's chat by its id, both
// themes: the auto preview omitted in every launch with the owner's reason
// naming both catalogue ids; the visual preview run as private with its
// state and its untested preview leg; a group entry of each set left to a
// launch on another chat, elsewhere in the state and in omittedRuns; the
// matrix and the notes counting only the permanent omissions against it.
[[nodiscard]] WidgetOpsSelfCheckRow CheckSelection() {
	auto control = ConformanceState();
	control.args.framework = u"v0.0.0"_q;
	control.args.chat = peerFromUser(UserId(BareId(1)));
	control.fixture = MakeWidgetFixture({ .chat = control.args.chat });
	auto ids = QSet<QString>();
	for (const auto json : {
			R"({"id":"auto/p","set":"auto","file":"p.html",)"
				R"("themes":["light"],"widths":[390],"message":)"
				R"([{"kind":"preview","chat_data":"c","public_data":"p"}]})",
			R"({"id":"auto/g","set":"auto","file":"g.html",)"
				R"("themes":["light"],"widths":[390],"message":)"
				R"([{"kind":"group"}]})",
			R"({"id":"auto/d","set":"auto","file":"d.html",)"
				R"("themes":["light"],"widths":[390]})",
			R"({"id":"visual/p","set":"visual","file":"vp.html",)"
				R"("themes":["light"],"widths":[390],"message":)"
				R"([{"kind":"preview","chat_data":"c","public_data":"p"}]})",
			R"({"id":"visual/g","set":"visual","file":"vg.html",)"
				R"("themes":["light"],"widths":[390],"message":)"
				R"([{"kind":"group"}]})" }) {
		auto test = ParseTest(
			QJsonDocument::fromJson(QByteArray(json)).object(),
			ids);
		test.html = QByteArray("<p>control</p>");
		test.read = true;
		control.tests.push_back(std::move(test));
	}
	SelectRuns(control);
	auto runs = QStringList();
	for (const auto &run : control.runs) {
		runs.push_back(run.key);
	}
	auto omitted = QStringList();
	for (const auto &entry : control.omitted) {
		omitted.push_back(u"%1|%2|%3"_q.arg(
			entry.id,
			entry.theme,
			entry.elsewhere ? u"elsewhere"_q : u"permanent"_q));
	}
	const auto preview = control.omitted.empty()
		? QString()
		: control.omitted.front().reason;
	const auto file = RunFileObject(control, u"self-check"_q);
	const auto matrix = file.value(u"matrix"_q).toObject();
	auto message = QJsonObject();
	for (const auto &entry : file.value(u"incompleteRuns"_q).toArray()) {
		const auto object = entry.toObject();
		if (object.value(u"id"_q).toString() == u"visual/p"_q) {
			message = object.value(u"message"_q).toObject();
		}
	}
	auto serialized = QJsonArray();
	for (const auto &entry : file.value(u"omittedRuns"_q).toArray()) {
		serialized.push_back(entry.toObject().value(u"elsewhere"_q));
	}
	auto permanentNote = false;
	auto elsewhereNote = false;
	auto previewNote = false;
	for (const auto &note : file.value(u"notes"_q).toArray()) {
		const auto text = note.toString();
		permanentNote = permanentNote
			|| (text.startsWith(u"auto/p in light is omitted: "_q)
				&& text.contains(u"a run file has 4 auto runs, not 6"_q));
		elsewhereNote = elsewhereNote
			|| text.startsWith(u"auto/g in light is not run in this "_q);
		previewNote = previewNote
			|| text.startsWith(u"visual/p asks for a preview"_q);
	}
	const auto passed = control.invalid.isEmpty()
		&& (runs == QStringList{
			RunKey(u"auto/d"_q, u"light"_q),
			RunKey(u"visual/p"_q, u"light"_q),
			RunKey(u"auto/d"_q, u"dark"_q),
		})
		&& (omitted == QStringList{
			u"auto/p|light|permanent"_q,
			u"auto/g|light|elsewhere"_q,
			u"visual/g|light|elsewhere"_q,
			u"auto/p|dark|permanent"_q,
			u"auto/g|dark|elsewhere"_q,
		})
		&& preview.contains(u"tw.preview"_q)
		&& preview.contains(u"behaviour.preview"_q)
		&& (serialized
			== QJsonArray{ false, true, true, false, true })
		&& (matrix.value(u"permanentAutoOmissions"_q).toInt() == 2)
		&& (matrix.value(u"fullAutoRuns"_q).toInt() == 4)
		&& (matrix.value(u"elsewhereAuto"_q).toInt() == 2)
		&& (matrix.value(u"elsewhereVisual"_q).toInt() == 1)
		&& (message.value(u"requestedKind"_q).toString() == u"preview"_q)
		&& (message.value(u"kind"_q).toString() == u"private"_q)
		&& (message.value(u"flags"_q).toObject()
			== QJsonObject{
				{ u"isCopy"_q, false },
				{ u"canQuery"_q, true },
				{ u"backgroundQuery"_q, true },
			})
		&& (message.value(u"state"_q).toObject()
			== QJsonObject{
				{ u"chatData"_q, u"c"_q },
				{ u"publicData"_q, u"p"_q },
			})
		&& message.contains(u"previewLeg"_q)
		&& !message.contains(u"derived"_q)
		&& permanentNote
		&& elsewhereNote
		&& previewNote;
	return SelfCheckRow(
		passed,
		u"selection self-check: in a user's chat an auto preview is omitted "
			u"in every launch naming tw.preview and behaviour.preview, a "
			u"visual preview runs as private with its state and its preview "
			u"leg untested, group entries are left to another chat with "
			u"elsewhere true, and only the permanent omissions count against "
			u"the matrix"_q,
		CompactJson(QJsonObject{
			{ u"runs"_q, Strings(runs) },
			{ u"omitted"_q, Strings(omitted) },
			{ u"elsewhere"_q, serialized },
			{ u"matrix"_q, matrix },
			{ u"message"_q, message },
			{ u"invalid"_q, Strings(control.invalid) },
		}));
}

// Defined with the totals, which it controls.
[[nodiscard]] WidgetOpsSelfCheckRow CheckTotalsCoverage();

// The tap's own scanner checks, the played bot's, the tap's probe-fixture
// checks, the pin's paint-policy fixture rows, then the pure result
// reader, summary reader, observation, run file, message context and bot
// table, selection, host reading, desktop stop, picture and totals
// controls; every row is one Check.
void RunSelfChecks(ConformanceState &state) {
	auto failed = QStringList();
	auto count = 0;
	const auto take = [&](const WidgetOpsSelfCheckRow &row) {
		Check(row.passed, row.name, row.details);
		++count;
		if (!row.passed) {
			failed.push_back(row.name);
		}
	};
	auto scanner = RunWidgetOpsSelfCheck();
	for (const auto &row : scanner.rows) {
		take(row);
	}
	state.control = std::move(scanner.control);
	for (const auto &row : RunWidgetBotSelfCheck()) {
		take(row);
	}
	if (state.probeFixture.isEmpty()) {
		take(SelfCheckRow(
			false,
			u"the probe fixture was read for the ops reader's probe "
				u"check"_q,
			state.packageError));
	} else {
		for (const auto &row
			: RunWidgetConformanceProbeCheck(state.probeFixture)) {
			take(row);
		}
	}
	for (const auto &row : RunPaintPolicyFixture(state.args.package)) {
		take(row);
	}
	for (const auto &row : CheckResultReader()) {
		take(row);
	}
	for (const auto &row : RunConformanceSummaryCheck()) {
		take(SelfCheckRow(row.passed, row.name, row.details));
	}
	for (const auto &row : RunConformanceObservationCheck()) {
		take(SelfCheckRow(row.passed, row.name, row.details));
	}
	for (const auto &row : CheckRunFileModel()) {
		take(row);
	}
	for (const auto &row : RunContextCheck()) {
		take(SelfCheckRow(row.passed, row.name, row.details));
	}
	take(CheckSelection());
	take(CheckHostReadings());
	for (const auto &row : RunDesktopStopCheck()) {
		take(SelfCheckRow(row.passed, row.name, row.details));
	}
	for (const auto &row : RunPictureCheck()) {
		take(SelfCheckRow(row.passed, row.name, row.details));
	}
	take(CheckRunFileHost());
	take(CheckTotalsCoverage());
	state.selfCheckRows = count;
	state.selfCheckFailed = failed;
	state.selfChecked = true;
}

// ---------------------------------------------------------------------
// The compose field.

// Before the injection: setInnerFocus() of the exact HistoryWidget, after
// which the window's focus child must be the field's editor. Nothing
// activates the window: QWidget::setFocus() sets an inactive window's
// focus child, and an activation would mark the chat read. The field's
// text is never touched.
void FocusCompose(ConformanceState &state, ConformanceRun &run) {
	state.currentRun = run.key;
	run.progress = u"focusing the chat's input field"_q;
	if (const auto owner = ComposeOwner(state)) {
		owner->setInnerFocus();
	}
	run.compose = ReadCompose(state);
	const auto &compose = run.compose;
	if (!compose.found) {
		AddRefusal(
			run,
			u"compose-unavailable"_q,
			compose.unusual.join(u"; "_q));
		return;
	}
	if (!compose.unusual.isEmpty()) {
		AddRefusal(
			run,
			u"compose-not-ordinary"_q,
			compose.unusual.join(u"; "_q));
	}
	if (!compose.focused) {
		AddRefusal(
			run,
			u"focus-not-compose"_q,
			u"after setInnerFocus() the window's focus child is %1, not the "
				u"field's editor %2"_q.arg(
					compose.holder,
					FocusText(compose.editor.data())));
	}
}

// chatFocused, CLIENTS.md's D4 reading before any input: the window's
// focus child is still the editor focused before the load. Left
// unmeasured when that focus was never established.
void ReadChatFocus(ConformanceRun &run) {
	if (!run.compose.focused) {
		return;
	}
	const auto editor = run.compose.editor.data();
	const auto window = editor ? editor->window() : nullptr;
	const auto holder = window ? window->focusWidget() : nullptr;
	run.chatFocused = editor && (holder == editor);
	if (!*run.chatFocused) {
		run.focusHolder = FocusText(holder);
	}
}

// ---------------------------------------------------------------------
// One run.

[[nodiscard]] bool HostLoaded(const WidgetOpsReading &reading) {
	return (reading.ackQueries > 0) && (reading.ackResults > 0);
}

// A group run is refused, never injected, when the loaded chat turns out
// a broadcast channel. The message gets its entry's context, no widget id,
// and its id as the caption below the widget, so the desktop gives its
// host CLIENTS.md's all-0 safe area; a safeArea entry's gets no caption, so
// the desktop's time goes over its corner. The empty override only
// watches, so every host keeps the product's options.
void InjectRun(ConformanceState &state, ConformanceRun &run) {
	run.progress = u"injecting"_q;
	const auto &test = state.tests[run.test];
	const auto history = WidgetFixtureHistory(*state.fixture);
	const auto wrong = history
		? ChatRefusal(test, history->peer)
		: QString();
	if (!wrong.isEmpty()) {
		run.notRun = u"not injected: "_q + wrong;
		return;
	}
	run.message->caption = test.safeArea ? QString() : test.id;
	run.message->watchHost = WidgetHostOverride();
	const auto refusal = InjectWidgetMessage(
		*state.fixture,
		*run.message,
		test.html,
		kWidth,
		0, // height
		test.message.fields);
	if (run.message->key) {
		run.bot = ArmWidgetBot(run.message->key, test.bot);
	}
	if (!refusal.isEmpty()) {
		run.notRun = refusal;
	}
}

[[nodiscard]] bool InjectLaidOut(
		const ConformanceState &state,
		ConformanceRun &run) {
	return !run.notRun.isEmpty()
		|| WidgetMessageLaidOut(*state.fixture, *run.message);
}

void CheckInjected(const ConformanceState &state, ConformanceRun &run) {
	if (!run.notRun.isEmpty()) {
		return;
	}
	const auto refusal = CheckWidgetMessage(*state.fixture, *run.message);
	if (!refusal.isEmpty()) {
		run.notRun = refusal;
		return;
	}
	run.progress = u"injected"_q;
	const auto item = WidgetFixtureItem(*state.fixture, run.message->id);
	const auto media = item
		? dynamic_cast<Tgw::WidgetMedia*>(item->media())
		: nullptr;
	if (media) {
		run.context = media->context();
	}
}

[[nodiscard]] crl::time WaitBound(ResultWait wait) {
	return (wait == ResultWait::Initial) ? kResultBound : kDoneBound;
}

[[nodiscard]] QString DeathText(bool frozen) {
	return frozen
		? u"the host sent a fatal frozen notice"_q
		: u"the runtime stopped the widget"_q;
}

void StartResultWait(ConformanceRun &run) {
	run.progress = u"waiting for the result"_q;
	const auto shown = run.loadShow.get();
	if (!shown || !shown->shown()) {
		AddRefusal(
			run,
			u"load-show-refused"_q,
			(shown && shown->finished)
				? (ChatListShowRefusalName(shown->refusal)
					+ u" - "_q
					+ shown->reason)
				: u"the load show did not run"_q);
	}
	StartFixtureWait(run.message->wait);
}

// The wanted status, a death or a failure, or the bound: the newest
// result, the tap's counts and the current runtime's state, read only.
[[nodiscard]] bool ResultDecided(
		const ConformanceState &state,
		ConformanceRun &run,
		ResultWait wait) {
	RefreshResult(run);
	auto &message = *run.message;
	const auto reading = ReadWidgetOps(*message.tap);
	const auto runtime = FixtureOwnRuntime(*state.fixture, message.id);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	const auto &result = run.result;
	message.wait.details = u"status=[%1] present=%2 error=[%3] generation=%4 "
		u"updates=%5 state=%6"_q.arg(
			result.status,
			NumberText(result.present ? 1 : 0),
			result.error,
			NumberText(result.generation),
			NumberText(result.updates),
			runtime ? RuntimeStateName(current) : u"no-runtime"_q)
		+ u" ackQueries=%1 ackResults=%2 frozen=%3"_q.arg(
			NumberText(reading.ackQueries),
			NumberText(reading.ackResults),
			NumberText(reading.fatalFrozen));
	return WantedStatus(state, run, wait)
		|| (reading.fatalFrozen > 0)
		|| !WidgetFixtureItem(*state.fixture, message.id)
		|| (current == RuntimeState::Stopped)
		|| (current == RuntimeState::Failed)
		|| FixtureBoundReached(message.wait, WaitBound(wait));
}

// What ended a wait, read as ResultDecided() reads it.
[[nodiscard]] QString WaitOutcome(
		const ConformanceState &state,
		const ConformanceRun &run,
		ResultWait wait,
		bool frozen,
		const Tgw::Runtime *runtime) {
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	return WantedStatus(state, run, wait)
		? run.result.status
		: frozen
		? u"died"_q
		: !WidgetFixtureItem(*state.fixture, run.message->id)
		? u"gone"_q
		: !runtime
		? u"no-runtime"_q
		: (current == RuntimeState::Stopped)
		? u"stopped"_q
		: (current == RuntimeState::Failed)
		? u"failed"_q
		: u"bound"_q;
}

// The card and the host as the result wait left them. A core width that
// is not kWidth and a scheme that is not the run's theme are measured
// runner refusals, never conditions waited for.
void ReadView(const ConformanceState &state, ConformanceRun &run) {
	auto &view = run.view;
	view = ViewReading();
	view.read = true;
	const auto shown = run.loadShow.get();
	if (shown && shown->finished) {
		view.list = shown->kind + u" "_q + shown->listIdentity;
		view.loadView = shown->viewIdentity;
	}
	const auto own = FixtureOwnView(*state.fixture, run.message->id);
	view.view = ChatListShowViewIdentity(own);
	const auto runtime = own ? Tgw::FindRuntime(own) : nullptr;
	if (own) {
		const auto card = ReadWidgetCard(own);
		view.card = card.card;
		view.state = card.state;
		view.core = card.core
			? (u"0x"_q + QString::number(quint64(card.core), 16))
			: u"none"_q;
	}
	if (const auto core = runtime ? runtime->core() : nullptr) {
		view.width = core->width();
	}
	const auto latest = ReadWidgetOpsLatest(*run.message->tap);
	view.scheme = QString::fromStdString(latest.scheme);
	view.themeMessage = QString::fromStdString(latest.themeMessage);
	view.generation = latest.generation;
	view.schemeChanges = latest.schemeChanges;
	if (view.width) {
		run.width = int(std::lround(*view.width));
		if (std::abs(*view.width - kWidth) > kWidthTolerance) {
			AddRefusal(
				run,
				u"width-mismatch"_q,
				u"the core is %1 CSS px wide, not %2: the width anchor was "
					u"not honoured, or the chat column is too narrow"_q.arg(
						QString::number(*view.width),
						NumberText(kWidth)));
		}
	} else if (run.loaded && !run.died) {
		AddRefusal(
			run,
			u"width-unmeasured"_q,
			u"the widget loaded, but its runtime showed no core to read "
				u"the width of"_q);
	}
	if (!view.scheme.isEmpty() && (view.scheme != run.theme)) {
		AddRefusal(
			run,
			u"scheme-mismatch"_q,
			u"the renderer was given the [%1] scheme in a %2 run"_q.arg(
				view.scheme,
				run.theme));
	} else if (view.scheme.isEmpty() && run.loaded && !run.died) {
		AddRefusal(
			run,
			u"scheme-unmeasured"_q,
			u"the widget loaded, but its newest host's renderer has no "
				u"scheme reading"_q);
	}
}

void RecordResult(const ConformanceState &state, ConformanceRun &run) {
	RefreshResult(run);
	auto &message = *run.message;
	const auto reading = ReadWidgetOps(*message.tap);
	const auto runtime = FixtureOwnRuntime(*state.fixture, message.id);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	const auto frozen = (reading.fatalFrozen > 0);
	const auto stopped = runtime && (current == RuntimeState::Stopped);
	run.resultMs = crl::now() - message.wait.started;
	run.runtimeState = runtime
		? RuntimeStateName(current)
		: u"no-runtime"_q;
	run.loaded = HostLoaded(reading);
	run.died = frozen || stopped;
	if (run.died) {
		run.dead = DeathText(frozen);
	}
	run.resultOutcome = WaitOutcome(
		state,
		run,
		ResultWait::Initial,
		frozen,
		runtime);
	run.progress = u"result: "_q + run.resultOutcome;
	ReadView(state, run);
	ReadChatFocus(run);
}

// The wait after the actions keeps what the first one measured; a death
// during the actions is recorded on top of it.
void RecordDone(const ConformanceState &state, ConformanceRun &run) {
	RefreshResult(run);
	auto &message = *run.message;
	const auto reading = ReadWidgetOps(*message.tap);
	const auto runtime = FixtureOwnRuntime(*state.fixture, message.id);
	const auto frozen = (reading.fatalFrozen > 0);
	const auto stopped = runtime
		&& (runtime->state() == RuntimeState::Stopped);
	run.doneMs = crl::now() - message.wait.started;
	run.loaded = run.loaded || HostLoaded(reading);
	if (!run.died && (frozen || stopped)) {
		run.died = true;
		run.dead = DeathText(frozen);
	}
	run.doneOutcome = WaitOutcome(
		state,
		run,
		ResultWait::AfterActions,
		frozen,
		runtime);
	run.progress = u"after the actions: "_q + run.doneOutcome;
}

// The "Died: " line Summarize() puts first, read only from a summary of
// the host the log was requested for. "terminated" is the runtime's own
// retirement, not a death. True when it took the host's reason.
[[nodiscard]] bool ReadDeath(ConformanceRun &run) {
	const auto &log = run.message->log;
	const auto own = (log.summaryState == WidgetLogSummary::Fresh)
		|| (log.summaryState == WidgetLogSummary::Final);
	const auto first = own
		? log.summary.section(QChar('\n'), 0, 0)
		: QString();
	if (!first.startsWith(u"Died: "_q)) {
		return false;
	}
	run.deathLine = first;
	const auto reason = first.mid(6).trimmed();
	if (reason == u"terminated"_q) {
		return false;
	}
	run.died = true;
	run.dead = reason;
	return true;
}

// The card's height in CSS px, CLIENTS.md's end bubble height, while the
// list's own view still shows it, and the core's content height beside it.
void ReadHeight(const ConformanceState &state, ConformanceRun &run) {
	const auto view = FixtureOwnView(*state.fixture, run.message->id);
	const auto card = view ? WidgetCardLocal(view) : QRect();
	if (!card.isEmpty()) {
		run.height = Tgw::CssFromLogical(card.height());
	}
	const auto runtime = view ? Tgw::FindRuntime(view) : nullptr;
	const auto core = runtime ? runtime->core() : nullptr;
	run.contentHeight = core ? core->height() : std::nullopt;
	if (!run.height && run.loaded) {
		AddRefusal(
			run,
			u"height-unmeasured"_q,
			u"the widget loaded, but the list showed no card of its own "
				u"to measure before the removal"_q);
	}
}

// The accepted bridge calls and the uncaught errors, from the raw summary
// of the run's only host; each refusal of that reading is the run's own.
void ReadBridgeAndErrors(ConformanceRun &run, int hosts) {
	run.summary = ReadConformanceSummary(run.message->log, hosts);
	run.summaryRead = true;
	run.bridge = run.summary.bridge;
	run.errors = run.summary.errors;
	for (const auto &refusal : run.summary.refusals) {
		AddRefusal(run, refusal.name, refusal.detail);
	}
}

void RecordLog(const ConformanceState &state, ConformanceRun &run) {
	auto &message = *run.message;
	FinishRuntimeLog(*state.fixture, message);
	RefreshResult(run);
	const auto reading = ReadWidgetOps(*message.tap);
	run.loaded = run.loaded || HostLoaded(reading);
	if ((reading.fatalFrozen > 0) && !run.died) {
		run.died = true;
		run.dead = DeathText(true);
	}
	// A stop the desktop decided itself may come after RecordDone().
	const auto desktopStop = ReadDeath(run)
		? QString()
		: ReadDesktopStop(*state.fixture, message, reading.fatalFrozen);
	if (!desktopStop.isEmpty()) {
		run.died = true;
		run.dead = desktopStop;
	}
	ReadSafeAreas(run);
	const auto &test = state.tests[run.test];
	for (const auto &refusal : {
			SafeAreaRefusal(test.safeArea, run.safeAreas, run.loaded),
			StopRefusal(
				test.stops,
				run.died,
				run.deathLine,
				!desktopStop.isEmpty()) }) {
		if (!refusal.name.isEmpty()) {
			AddRefusal(run, refusal.name, refusal.detail);
		}
	}
	ReadHeight(state, run);
	ReadBridgeAndErrors(run, reading.factoriesSelected);
	run.measured = true;
	run.progress = u"logged"_q;
	const auto &log = message.log;
	Row(u"CONFORMANCE_LOG"_q, {
		{ u"run"_q, run.key },
		{ u"source"_q, log.source },
		{ u"summaryState"_q, SummaryStateName(log.summaryState) },
		{ u"summary"_q, ConformanceSummaryObject(run.summary) },
		{ u"pending"_q, log.pending },
		{ u"waitedMs"_q, qint64(crl::now() - message.wait.started) },
		{ u"chars"_q, log.chars },
		{ u"truncated"_q, (log.chars > kMaxLogChars) },
		{ u"text"_q, log.text.left(kMaxLogChars) },
	});
}

// The final reading once the hosts finished, or the bound; a run is
// complete only with that reading final. Hosts that did not finish stop
// every later injection.
void FinalizeRun(ConformanceState &state, ConformanceRun &run) {
	const auto &message = *run.message;
	run.barrierMs = crl::now() - message.removedAt;
	run.reading = message.tap
		? ReadWidgetOps(*message.tap)
		: WidgetOpsReading();
	run.readingFinal = message.tap && WidgetTapFinished(run.reading);
	run.itemGone = !WidgetFixtureItem(*state.fixture, message.id);
	if (!run.readingFinal && state.abort.isEmpty()) {
		state.abort = u"not run: the hosts of %1 had not finished %2 ms "
			u"after its removal (factories %3 of %4 released, wrappers %5 "
			u"of %6 destroyed), so no later run was started"_q.arg(
				run.key,
				NumberText(run.barrierMs),
				NumberText(run.reading.factoriesReleased),
				NumberText(run.reading.factoriesSelected),
				NumberText(run.reading.wrappersDestroyed),
				NumberText(run.reading.wrappersBuilt));
	}
	run.completed = run.measured
		&& run.readingFinal
		&& run.notRun.isEmpty();
	run.progress = !run.readingFinal
		? u"removed, its hosts not finished"_q
		: run.completed
		? u"final"_q
		: u"removed"_q;
	state.currentRun = QString();
	auto row = RunObject(state, run);
	row.insert(u"item"_q, IdText(message.id));
	row.insert(u"completed"_q, run.completed);
	row.insert(u"progress"_q, run.progress);
	row.insert(u"notRun"_q, run.notRun);
	row.insert(u"barrierMs"_q, qint64(run.barrierMs));
	row.insert(u"itemGone"_q, run.itemGone);
	row.insert(u"fixture"_q, message.shape);
	Row(u"CONFORMANCE_RUN"_q, row);
}

// Every run not completed keeps what its tap read last, before the
// release disarms it; nothing here reads a runtime.
void SaveReadings(ConformanceState &state) {
	for (auto &run : state.runs) {
		if (!run.completed && run.message && run.message->tap) {
			run.reading = ReadWidgetOps(*run.message->tap);
			RefreshResult(run);
		}
	}
}

// ---------------------------------------------------------------------
// The stage snapshot and its crops.

// The union of the run's stage rects, in CSS px from the widget's top-left.
[[nodiscard]] QRect StageUnion(const ConformanceRun &run) {
	auto result = QRect();
	for (const auto &stage : run.stages) {
		result = result.united(stage.rect);
	}
	return result;
}

// |css| on the current card of |view|, in |view|'s coordinates.
[[nodiscard]] QRect SnapshotLocal(ChatListShowView view, QRect css) {
	const auto card = WidgetCardLocal(view);
	return card.isEmpty()
		? QRect()
		: LogicalFromCss(css).translated(card.topLeft());
}

[[nodiscard]] QString SnapshotSkip(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto skip = RunSkip(state, run);
	if (!skip.isEmpty()) {
		return skip;
	}
	const auto block = SnapshotBlock(run);
	return block.name.isEmpty()
		? QString()
		: (block.name + u": "_q + block.detail);
}

void RefuseStage(
		StageReading &stage,
		const QString &name,
		const QString &detail) {
	stage.refusal = name;
	stage.capture.insert(u"detail"_q, detail);
}

// The snapshot's refusal, and every stage's that is not cropped.
void RefuseSnapshot(
		ConformanceRun &run,
		const QString &name,
		const QString &detail) {
	run.snapshot.refusal = name;
	run.snapshot.detail = detail;
	for (auto &stage : run.stages) {
		if (stage.crop.isEmpty()) {
			RefuseStage(stage, name, detail);
		}
	}
}

// The rect the widget measured for a stage itself, as it published it.
[[nodiscard]] QJsonValue WidgetStageRect(
		const ConformanceRun &run,
		const QString &name) {
	const auto value = ReportedResult(run).value.value(
		u"stages"_q).toObject().value(name);
	return value.isUndefined() ? QJsonValue(QJsonValue::Null) : value;
}

// The manifest rect of stage |index| cut out of the accepted snapshot
// |image|, from (rect.x - origin.x, rect.y - origin.y) * kCropScale, and
// saved; the widget's own rect for it is logged beside, never cut at.
void CropStage(
		const ConformanceState &state,
		ConformanceRun &run,
		const QImage &image,
		int index) {
	auto &stage = run.stages[index];
	const auto origin = run.snapshot.css.topLeft();
	const auto pixels = QRect(
		(stage.rect.topLeft() - origin) * kCropScale,
		stage.rect.size() * kCropScale);
	const auto card = run.snapshotCard.card;
	const auto local = LogicalFromCss(stage.rect).translated(card.topLeft());
	stage.capture = QJsonObject{
		{ u"manifestRect"_q, RectArray(stage.rect) },
		{ u"widgetRect"_q, WidgetStageRect(run, stage.name) },
		{ u"snapshotOrigin"_q, QJsonArray{ origin.x(), origin.y() } },
		{ u"pixelRect"_q, RectArray(pixels) },
		{ u"local"_q, RectArray(local) },
	};
	if (!card.contains(local)) {
		RefuseStage(
			stage,
			u"out-of-bounds"_q,
			u"the stage's rect %1 in the view is not inside the painted "
				u"card %2"_q.arg(RectText(local), RectText(card)));
		return;
	}
	const auto crop = image.rect().contains(pixels)
		? image.copy(pixels).convertToFormat(QImage::Format_RGB32)
		: QImage();
	if (crop.size() != pixels.size()) {
		RefuseStage(
			stage,
			u"out-of-bounds"_q,
			u"the pixel rect %1 is not inside the %2x%3 snapshot"_q.arg(
				RectText(pixels),
				NumberText(image.width()),
				NumberText(image.height())));
		return;
	}
	const auto file = u"crops/%1.%2.png"_q.arg(
		FileBase(run),
		FilePart(stage.name));
	const auto error = SavePng(state, crop, file);
	if (!error.isEmpty()) {
		RefuseStage(stage, u"write-failed"_q, error);
		return;
	}
	stage.crop = file;
	stage.refusal = QString();
	stage.capture.insert(u"file"_q, file);
	stage.capture.insert(
		u"size"_q,
		QJsonArray{ crop.width(), crop.height() });
}

// In the stage after the snapshot show and its date wait: the same view,
// core, card and geometry, the accepted date wait, one accepted capture of
// the stage union from the list's own paint, its 2x checked on the image
// itself, then each stage's crop.
void TakeSnapshot(const ConformanceState &state, ConformanceRun &run) {
	auto &snapshot = run.snapshot;
	snapshot.css = StageUnion(run);
	snapshot.ratio = style::DevicePixelRatio();
	snapshot.scale = style::Scale();
	const auto block = SnapshotBlock(run);
	if (!block.name.isEmpty()) {
		RefuseSnapshot(run, block.name, block.detail);
		return;
	}
	const auto shown = run.snapshotShow.get();
	if (!shown || !shown->shown()) {
		RefuseSnapshot(
			run,
			u"show-refused"_q,
			(shown && shown->finished)
				? (ChatListShowRefusalName(shown->refusal)
					+ u" - "_q
					+ shown->reason)
				: u"the snapshot show did not run"_q);
		return;
	}
	// The snapshot show hovers nothing on purpose; only a press needs one.
	auto unhovered = *shown;
	unhovered.hovered = true;
	const auto changed = RevalidateWidgetCard(
		*state.fixture,
		run.message->id,
		u"snapshot"_q,
		unhovered,
		run.snapshotCard);
	if (!changed.isEmpty()) {
		RefuseSnapshot(run, u"capture-refused"_q, changed);
		return;
	}
	const auto date = DateWaitRefusal(run.snapshotDate.get());
	if (!date.name.isEmpty()) {
		RefuseSnapshot(run, date.name, date.detail);
		return;
	}
	const auto list = WidgetFixtureList(*state.fixture);
	auto capture = PreparedWidgetCapture();
	if (!capture.prepare(list, list, shown->rect)) {
		RefuseSnapshot(run, u"capture-refused"_q, capture.pendingReason());
		return;
	}
	const auto &image = capture.image();
	snapshot.list = shown->rect;
	snapshot.image = image.size();
	const auto expected = snapshot.css.size() * kCropScale;
	if (image.size() != expected) {
		RefuseSnapshot(
			run,
			u"not-2x"_q,
			u"the accepted %1x%2 image of the %3x%4 CSS px union is not "
				u"%5x%6"_q.arg(
					NumberText(image.width()),
					NumberText(image.height()),
					NumberText(snapshot.css.width()),
					NumberText(snapshot.css.height()),
					NumberText(expected.width()),
					NumberText(expected.height())));
		return;
	}
	const auto file = u"cards/%1.png"_q.arg(FileBase(run));
	const auto error = SavePng(state, image, file);
	if (error.isEmpty()) {
		snapshot.file = file;
	} else {
		AddRefusal(run, u"card-write-failed"_q, error);
	}
	snapshot.refusal = QString();
	for (auto i = 0; i != int(run.stages.size()); ++i) {
		CropStage(state, run, image, i);
	}
}

void RecordSnapshot(const ConformanceState &state, ConformanceRun &run) {
	TakeSnapshot(state, run);
	run.progress = run.snapshot.refusal.isEmpty()
		? u"snapshot taken"_q
		: (u"snapshot refused: "_q + run.snapshot.refusal);
	auto stages = QJsonArray();
	for (const auto &stage : run.stages) {
		stages.push_back(QJsonObject{
			{ u"name"_q, stage.name },
			{ u"crop"_q, stage.crop },
			{ u"refusal"_q, stage.refusal },
			{ u"capture"_q, stage.capture },
		});
	}
	auto row = SnapshotObject(run);
	row.insert(u"run"_q, run.key);
	row.insert(u"stages"_q, stages);
	Row(u"CONFORMANCE_SNAPSHOT"_q, row);
}

// Focus, injection, the load show and the result wait: everything a run
// does before its actions.
void AppendRunStart(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	const auto key = state->runs[index].key;
	const auto prefix = Prefixed(key + u": "_q);
	const auto entry = [=]() -> ConformanceRun& {
		return state->runs[index];
	};
	const auto skip = [=] {
		return RunSkip(*state, entry());
	};
	runner->add({
		.name = prefix + u"focus the chat's input field"_q,
		.skipReason = skip,
		.run = [=] {
			FocusCompose(*state, entry());
		},
	});
	runner->add({
		.name = prefix + u"arm the tap and inject the widget message"_q,
		.skipReason = skip,
		.run = [=] {
			InjectRun(*state, entry());
		},
		.until = [=] {
			return InjectLaidOut(*state, entry());
		},
		.then = [=] {
			CheckInjected(*state, entry());
		},
		.timeout = kWidgetLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return entry().message->wait.details;
		},
	});
	entry().loadShow = AppendShowInChatList(runner, {
		.name = u"widget conformance "_q + key + u" load"_q,
		.resolve = [=] {
			return ChatListShowTarget{
				WidgetFixtureList(*state->fixture),
				entry().message->id,
			};
		},
		.rect = [](ChatListShowView view) {
			return LoadRect(WidgetCardLocal(view));
		},
		.ready = [](ChatListShowView view) {
			const auto card = ReadWidgetCard(view);
			return card.html && card.runtime;
		},
		.readyDetails = [](ChatListShowView view) {
			return WidgetCardText(ReadWidgetCard(view));
		},
		.skipReason = skip,
		.timeout = kShowTimeout,
	});
	runner->add({
		.name = prefix + u"wait for the result, a death or a failure"_q,
		.skipReason = skip,
		.run = [=] {
			StartResultWait(entry());
		},
		.until = [=] {
			return ResultDecided(*state, entry(), ResultWait::Initial);
		},
		.then = [=] {
			RecordResult(*state, entry());
		},
		.timeout = kResultBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return entry().message->wait.details;
		},
	});
}

// CLIENTS.md's wait for done after the actions, which go right before it;
// appended only for a run with actions.
void AppendDoneWait(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	if (state->runs[index].actions.empty()) {
		return;
	}
	const auto prefix = Prefixed(state->runs[index].key + u": "_q);
	const auto entry = [=]() -> ConformanceRun& {
		return state->runs[index];
	};
	runner->add({
		.name = prefix + u"wait for done after the actions"_q,
		.skipReason = [=] {
			return RunSkip(*state, entry());
		},
		.run = [=] {
			entry().progress = u"waiting for done after the actions"_q;
			StartFixtureWait(entry().message->wait);
		},
		.until = [=] {
			return ResultDecided(*state, entry(), ResultWait::AfterActions);
		},
		.then = [=] {
			RecordDone(*state, entry());
		},
		.timeout = kDoneBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return entry().message->wait.details;
		},
	});
}

// The show of the union of the run's stage rects, ready and with no hover,
// the wait for the floating date, then the snapshot and the crops;
// appended only for a run with stages in its theme.
void AppendSnapshot(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	if (state->runs[index].stages.empty()) {
		return;
	}
	const auto key = state->runs[index].key;
	const auto prefix = Prefixed(key + u": "_q);
	const auto css = StageUnion(state->runs[index]);
	const auto entry = [=]() -> ConformanceRun& {
		return state->runs[index];
	};
	entry().snapshotShow = AppendShowInChatList(runner, {
		.name = u"widget conformance "_q + key + u" snapshot"_q,
		.resolve = [=] {
			return ChatListShowTarget{
				WidgetFixtureList(*state->fixture),
				entry().message->id,
			};
		},
		.rect = [=](ChatListShowView view) {
			return SnapshotLocal(view, css);
		},
		.ready = [=](ChatListShowView view) {
			const auto card = ReadWidgetCard(view);
			if (!WidgetCardReady(card)) {
				return false;
			}
			entry().snapshotCard = card;
			return true;
		},
		.readyDetails = [](ChatListShowView view) {
			return WidgetCardText(ReadWidgetCard(view));
		},
		.skipReason = [=] {
			return SnapshotSkip(*state, entry());
		},
		.timeout = kShowTimeout,
	});
	entry().snapshotDate = AppendDateWait(
		runner,
		prefix + u"wait out the floating date before the snapshot"_q,
		[=] {
			return SnapshotSkip(*state, entry());
		},
		entry().snapshotShow);
	runner->add({
		.name = prefix + u"snapshot the stages at 2x and crop them"_q,
		.skipReason = [=] {
			return RunSkip(*state, entry());
		},
		.run = [=] {
			RecordSnapshot(*state, entry());
		},
	});
}

// The widget message is armed or in the chat, or a filler is.
[[nodiscard]] bool RunInjected(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto injected = [&](const WidgetFixtureMessage &message) {
		return WidgetFixtureItem(*state.fixture, message.id) != nullptr;
	};
	return run.message->armed
		|| injected(*run.message)
		|| std::any_of(begin(run.fillers), end(run.fillers), [&](
				const std::shared_ptr<WidgetFixtureMessage> &filler) {
			return injected(*filler);
		});
}

// The runtime log and the card height while the view exists, then the
// removal of the widget and its fillers and the host barrier, then the
// run's entry.
void AppendRunEnd(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	const auto prefix = Prefixed(state->runs[index].key + u": "_q);
	const auto entry = [=]() -> ConformanceRun& {
		return state->runs[index];
	};
	runner->add({
		.name = prefix + u"read the runtime log and the card height"_q,
		.skipReason = [=] {
			return RunSkip(*state, entry());
		},
		.run = [=] {
			RequestRuntimeLog(*state->fixture, entry().message);
		},
		.until = [=] {
			return RuntimeLogDecided(*state->fixture, *entry().message);
		},
		.then = [=] {
			RecordLog(*state, entry());
		},
		.timeout = kWidgetLogBound + kDefaultStageTimeout,
	});
	runner->add({
		.name = prefix + u"remove the message and wait for its hosts"_q,
		.skipReason = [=] {
			return RunInjected(*state, entry())
				? QString()
				: u"nothing was armed or injected for this run"_q;
		},
		.run = [=] {
			entry().progress = u"removing"_q;
			if (entry().bot) {
				DisarmWidgetBot(entry().message->key);
			}
			RemoveFixtureMessage(*state->fixture, *entry().message);
			RemoveRunFillers(*state, entry());
		},
		.until = [=] {
			return FixtureBarrierDecided(*entry().message);
		},
		.then = [=] {
			FinalizeRun(*state, entry());
		},
		.timeout = kWidgetBarrierBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			const auto &tap = entry().message->tap;
			return tap
				? CompactJson(TapObject(ReadWidgetOps(*tap), false))
				: u"no tap"_q;
		},
	});
}

// ---------------------------------------------------------------------
// The palette and the outgoing-message watcher.

void JudgePalettePhase(const ConformanceState &state, int index) {
	const auto &phase = state.palette[index];
	if (phase.restore) {
		JudgeRestore(
			phase.previous,
			phase.restore,
			Prefixed(u"the palette switch before the %1 runs is restored "
				u"first"_q.arg(phase.theme)));
	}
	const auto &switched = *phase.switched;
	if (switched.finished) {
		Check(
			switched.succeeded(),
			Prefixed(u"the app shows the %1 palette for the %2 runs"_q.arg(
				PaletteModeName(switched.requested),
				phase.theme)),
			PaletteSwitchText(switched));
	} else {
		Note(Prefixed(u"the palette switch for the %1 runs did not run, "
			u"and neither do they: %2"_q.arg(
				phase.theme,
				GlobalSkip(state))));
	}
}

// Restores the chain's last switch, when there is one (the phase before,
// or a theme action of its last run), then switches to the theme's
// palette, then judges both. The switch is the gate of the phase's runs.
void AppendPalettePhase(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		const QString &theme) {
	auto phase = PalettePhase{ .theme = theme };
	if (state->paletteCurrent) {
		phase.previous = state->paletteCurrent;
		phase.restore = AppendPaletteRestore(
			runner,
			phase.previous,
			Prefixed(u"before the %1 runs"_q.arg(theme)));
	}
	phase.switched = AppendPaletteSwitch(runner, {
		.name = Prefixed(u"the %1 runs"_q.arg(theme)),
		.mode = (theme == u"dark"_q) ? PaletteMode::Night : PaletteMode::Day,
		.skipReason = [=] {
			return GlobalSkip(*state);
		},
	});
	state->paletteCurrent = phase.switched;
	state->palette.push_back(std::move(phase));
	const auto index = int(state->palette.size()) - 1;
	runner->add({
		.name = Prefixed(u"judge the palette of the %1 runs"_q.arg(theme)),
		.run = [=] {
			JudgePalettePhase(*state, index);
		},
	});
}

// The palette the chain's first switch found, the launch's, is back after
// the final restore: its night mode, follow flag and sample.
void JudgeBaseline(const ConformanceState &state) {
	const auto &first = *state.palette.front().switched;
	const auto &restore = *state.paletteRestore;
	const auto details = u"launch{%1} sample=%2 end{%3} sample=%4"_q.arg(
		PaletteModesText(first.before),
		first.sampleBefore.name(),
		PaletteModesText(restore.after),
		restore.sampleAfter.name());
	if (!first.finished || !restore.finished) {
		Note(Prefixed(u"the launch palette is not judged: the first switch "
			u"or the final restore did not run - "_q) + details);
		return;
	}
	Check(
		(restore.after.night == first.before.night)
			&& (restore.after.follow == first.before.follow)
			&& (restore.sampleAfter == first.sampleBefore),
		Prefixed(u"the palette after the last run is the one the launch "
			u"began in"_q),
		details);
}

void AppendPaletteEnd(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state) {
	const auto last = state->paletteCurrent;
	if (!last) {
		return;
	}
	state->paletteRestore = AppendPaletteRestore(
		runner,
		last,
		Prefixed(u"after the last run"_q));
	runner->add({
		.name = Prefixed(u"judge the palette after the last run"_q),
		.run = [=] {
			JudgeRestore(
				last,
				state->paletteRestore,
				Prefixed(u"the last palette switch is restored after the "
					u"last run"_q));
			JudgeBaseline(*state);
		},
	});
}

void RecordOutgoing(ConformanceState &state, not_null<HistoryItem*> item) {
	if (!item->out() || WidgetFixtureOwns(*state.fixture, item->fullId())) {
		return;
	}
	auto entry = OutgoingItem{
		.run = state.currentRun,
		.id = IdText(item->fullId()),
		.wallMs = WallNow(),
		.inChat = (item->history().get()
			== WidgetFixtureHistory(*state.fixture)),
		.local = item->isLocal(),
		.regular = item->isRegular(),
		.service = item->isService(),
	};
	Row(u"CONFORMANCE_OUTGOING"_q, OutgoingObject(entry));
	state.outgoing.push_back(std::move(entry));
}

// On the fixture's session, resolved by the fixture's open. Every item
// the fixture registers has its id before History::addNewLocalMessage()
// can notify, so only an outgoing item the runner did not inject counts.
void StartWatcher(const std::shared_ptr<ConformanceState> &state) {
	const auto session = WidgetFixtureSession(*state->fixture);
	if (!session) {
		state->watcherError = u"not run: the fixture has no session to "
			u"watch for outgoing messages"_q;
		return;
	}
	const auto weak = std::weak_ptr<ConformanceState>(state);
	session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (const auto strong = weak.lock()) {
			RecordOutgoing(*strong, item);
		}
	}, state->watcher);
	state->watching = true;
	Note(Prefixed(u"watching the session's new items for outgoing messages "
		u"the runner did not inject"_q));
}

void StopWatcher(ConformanceState &state) {
	state.watcher.destroy();
	state.watching = false;
}

void CheckOutgoing(const ConformanceState &state) {
	auto items = QStringList();
	for (const auto &item : state.outgoing) {
		items.push_back(CompactJson(OutgoingObject(item)));
	}
	Check(
		state.outgoing.empty(),
		Prefixed(u"no outgoing message the runner did not inject was added "
			u"while the widgets ran"_q),
		u"unexpected=%1 [%2]"_q.arg(
			NumberText(int(state.outgoing.size())),
			items.join(u"; "_q)));
}

// ---------------------------------------------------------------------
// The stages.

void ReportPackage(ConformanceState &state) {
	CaptureEnvironment(state);
	PrepareOutput(state);
	const auto matrix = ComputeMatrix(state);
	auto keys = QJsonArray();
	auto refused = QStringList();
	for (const auto &run : state.runs) {
		keys.push_back(run.key);
		if (!run.notRun.isEmpty()) {
			refused.push_back(run.key + u": "_q + run.notRun);
		}
	}
	const auto network = WidgetFixtureNetwork(*state.fixture);
	Row(u"CONFORMANCE_PACKAGE"_q, {
		{ u"dir"_q, state.args.package },
		{ u"error"_q, state.packageError },
		{ u"framework"_q, state.args.framework },
		{ u"engine"_q, EngineText(state) },
		{ u"tests"_q, int(state.tests.size()) },
		{ u"probeFixtureBytes"_q, int(state.probeFixture.size()) },
		{ u"selection"_q, SelectionObject(state) },
		{ u"invalid"_q, Strings(state.invalid) },
		{ u"refused"_q, Strings(refused) },
		{ u"runs"_q, keys },
		{ u"omittedRuns"_q, OmittedArray(state) },
		{ u"matrix"_q, MatrixObject(matrix) },
		{ u"output"_q, state.outputDir },
		{ u"outputError"_q, state.outputError },
		{ u"network"_q, network.isEmpty() ? u"available"_q : network },
	});
	for (const auto &note : Notes(state, matrix)) {
		Note(Prefixed(u"run file note: "_q) + note);
	}
	Check(
		state.packageError.isEmpty(),
		Prefixed(u"the package's manifest and probe fixture were read"_q),
		state.packageError.isEmpty()
			? u"tests=%1 probeFixtureBytes=%2"_q.arg(
				NumberText(int(state.tests.size())),
				NumberText(state.probeFixture.size()))
			: state.packageError);
	Check(
		state.unknownThemes.isEmpty()
			&& state.unknownSets.isEmpty()
			&& state.unknownIds.isEmpty()
			&& state.unknownPictures.isEmpty()
			&& !state.args.framework.isEmpty()
			&& !state.args.engine.isEmpty(),
		Prefixed(u"every requested theme, set and id is the manifest's, every "
			u"requested picture a visual entry's, and the framework tag and "
			u"the twidget pin are named"_q),
		u"unknownThemes=[%1] unknownSets=[%2] unknownIds=[%3] "
			u"unknownPictures=[%4] framework=[%5] engine=[%6]"_q.arg(
				state.unknownThemes.join(u","_q),
				state.unknownSets.join(u","_q),
				state.unknownIds.join(u","_q),
				state.unknownPictures.join(u","_q),
				state.args.framework,
				state.args.engine));
	Check(
		state.invalid.isEmpty() && refused.isEmpty(),
		Prefixed(u"every selected manifest entry is well formed and its "
			u"HTML was read"_q),
		u"invalid=[%1] refused=[%2]"_q.arg(
			state.invalid.join(u"; "_q),
			refused.join(u"; "_q)));
	Check(
		!state.runs.empty(),
		Prefixed(u"the selection holds at least one run"_q),
		u"runs=%1 omitted=%2"_q.arg(
			NumberText(int(state.runs.size())),
			NumberText(int(state.omitted.size()))));
	Check(
		state.outputError.isEmpty(),
		Prefixed(u"the output directory is fresh and ready"_q),
		state.outputError.isEmpty() ? state.runFile : state.outputError);
}

// The totals: the runs that did not complete; the focus, refusals,
// actions and crops of the completed ones; and the scan counts of every
// run's last tap reading, final for a completed run.
struct Tallies {
	QStringList incomplete;
	QStringList notLoaded;
	QStringList dead;
	QStringList bound;
	QStringList doneBound;
	QStringList consoleLimited;
	QStringList unfocused;
	QStringList refused;
	QStringList missingActions;
	QStringList missingCrops;
	QStringList leaking;
	QStringList gapped;
	QStringList unfinished;
	QStringList telegramLinks;
	QStringList layered;
	QJsonObject refusals;
	ConformanceAbsences absences;
	int completed = 0;
	int loaded = 0;
	int focused = 0;
	int actions = 0;
	int performed = 0;
	int skipped = 0;
	int crops = 0;
	int saved = 0;
	int bridgeObserved = 0;
	int leaks = 0;
	int notes = 0;
	int gaps = 0;
	int violations = 0;
	int batches = 0;
	int layers = 0;
};

void CountScan(Tallies &tallies, const ConformanceRun &run) {
	const auto &reading = run.reading;
	tallies.leaks += reading.leaks;
	tallies.notes += reading.notes;
	tallies.gaps += reading.scanGaps;
	tallies.violations += reading.violations;
	tallies.batches += reading.batches;
	if (reading.leaks) {
		tallies.leaking.push_back(run.key);
	}
	if (reading.scanGaps) {
		tallies.gapped.push_back(run.key);
	}
	if (run.message->removed && !run.readingFinal) {
		tallies.unfinished.push_back(run.key);
	}
}

// The accepted openTelegramLink calls of every run with a bridge reading,
// and the layers hidden over its messages.
void CountEgress(Tallies &tallies, const ConformanceRun &run) {
	if (run.bridge) {
		for (const auto &call : *run.bridge) {
			if (call.method == u"openTelegramLink"_q) {
				tallies.telegramLinks.push_back(run.key + u": "_q
					+ QString::fromUtf8(QJsonDocument(call.args).toJson(
						QJsonDocument::Compact)));
			}
		}
	}
	auto layers = run.message->layers;
	for (const auto &filler : run.fillers) {
		layers += filler->layers;
	}
	if (!layers.isEmpty()) {
		tallies.layers += int(layers.size());
		tallies.layered.push_back(run.key + u": "_q + layers.join(u", "_q));
	}
}

// What a run's console section did not show: readings, not refusals.
[[nodiscard]] QStringList ConsoleLimits(const ConformanceSummary &summary) {
	auto result = QStringList();
	if (summary.consoleOmitted > 0) {
		result.push_back(u"%1 earlier console entries not shown"_q.arg(
			NumberText(summary.consoleOmitted)));
	}
	if (summary.consoleDropped) {
		result.push_back(u"the host kept no later console entries"_q);
	}
	if (summary.errorsClipped > 0) {
		result.push_back(u"%1 uncaught errors clipped"_q.arg(
			NumberText(summary.errorsClipped)));
	}
	return result;
}

void CountCompleted(Tallies &tallies, const ConformanceRun &run) {
	++tallies.completed;
	if (run.loaded) {
		++tallies.loaded;
	} else {
		tallies.notLoaded.push_back(run.key);
	}
	if (run.died) {
		tallies.dead.push_back(run.key + u": "_q + run.dead);
	}
	if (run.resultOutcome == u"bound"_q) {
		tallies.bound.push_back(run.key);
	}
	if (run.doneOutcome == u"bound"_q) {
		tallies.doneBound.push_back(run.key);
	}
	const auto limits = ConsoleLimits(run.summary);
	if (!limits.isEmpty()) {
		tallies.consoleLimited.push_back(
			run.key + u": "_q + limits.join(u", "_q));
	}
	if (run.chatFocused && *run.chatFocused) {
		++tallies.focused;
	} else {
		tallies.unfocused.push_back(run.key + (run.chatFocused
			? (u": the window's focus child is "_q + run.focusHolder)
			: u": unmeasured, the field was never focused"_q));
	}
	for (const auto &refusal : run.refusals) {
		tallies.refusals.insert(
			refusal.name,
			tallies.refusals.value(refusal.name).toInt() + 1);
		tallies.refused.push_back(run.key + u": "_q + refusal.name);
	}
	for (const auto &action : run.actions) {
		++tallies.actions;
		if (action.performed) {
			++tallies.performed;
		} else if (!action.skipReason.isEmpty()) {
			++tallies.skipped;
		} else {
			tallies.missingActions.push_back(u"%1 #%2 %3 (%4)"_q.arg(
				run.key,
				NumberText(action.index),
				action.kind,
				action.refusal));
		}
	}
	for (const auto &stage : run.stages) {
		++tallies.crops;
		if (!stage.crop.isEmpty()) {
			++tallies.saved;
		} else {
			tallies.missingCrops.push_back(u"%1 %2 (%3)"_q.arg(
				run.key,
				stage.name,
				stage.refusal));
		}
	}
	if (run.bridge) {
		++tallies.bridgeObserved;
	}
}

// The run as the totals' absence judgements read it.
[[nodiscard]] ConformanceCoverage CoverageOf(const ConformanceRun &run) {
	return {
		.key = run.key,
		.reading = run.reading,
		.bridge = run.bridge,
		.bridgeRefusals = ObservationOf(run).bridgeRefusals,
		.tapped = run.message && run.message->tap,
		.final = run.readingFinal,
	};
}

// The counts of every run's last tap reading and bridge reading, and the
// absences judged over every selected run, the scan's control leaks being
// the self-check's.
[[nodiscard]] Tallies CountTallies(const ConformanceState &state) {
	auto result = Tallies();
	auto coverage = std::vector<ConformanceCoverage>();
	for (const auto &run : state.runs) {
		if (run.message && run.message->tap) {
			CountScan(result, run);
		}
		if (run.message) {
			CountEgress(result, run);
		}
		if (run.completed) {
			CountCompleted(result, run);
		} else {
			result.incomplete.push_back(run.key + u": "_q + run.progress);
		}
		coverage.push_back(CoverageOf(run));
	}
	result.absences = JudgeConformanceAbsences(
		coverage,
		state.control.leaks);
	return result;
}

[[nodiscard]] QString AbsenceText(const ConformanceAbsence &absence) {
	const auto undecidable = !absence.selected
		|| !absence.uncovered.isEmpty();
	return u"%1selected=%2 covered=%3 uncovered=[%4]"_q.arg(
		undecidable
			? u"undecidable, not every selected run was observed: "_q
			: QString(),
		NumberText(absence.selected),
		NumberText(absence.selected - int(absence.uncovered.size())),
		absence.uncovered.join(u"; "_q));
}

[[nodiscard]] QJsonObject AbsencesObject(const ConformanceAbsences &all) {
	return QJsonObject{
		{ u"leaks"_q, ConformanceAbsenceObject(all.leaks) },
		{ u"gaps"_q, ConformanceAbsenceObject(all.gaps) },
		{ u"telegramLinks"_q, ConformanceAbsenceObject(all.telegramLinks) },
	};
}

// CountTallies() with the scan's control leaks matched over two selected
// runs no tap was armed for: one registered, with a bridge reading of no
// call, one with no reading at all.
WidgetOpsSelfCheckRow CheckTotalsCoverage() {
	auto control = ConformanceState();
	control.control.leaks = 2;
	control.control.batches = 2;
	const auto x = RunKey(u"auto/x"_q, u"light"_q);
	const auto y = RunKey(u"auto/y"_q, u"dark"_q);
	auto tapless = ConformanceRun{
		.id = u"auto/x"_q,
		.set = u"auto"_q,
		.theme = u"light"_q,
		.key = x,
	};
	tapless.message = std::make_shared<WidgetFixtureMessage>();
	tapless.bridge = std::vector<BridgeCall>();
	control.runs.push_back(std::move(tapless));
	control.runs.push_back(ConformanceRun{
		.id = u"auto/y"_q,
		.set = u"auto"_q,
		.theme = u"dark"_q,
		.key = y,
	});
	const auto absences = CountTallies(control).absences;
	const auto named = [](
			const ConformanceAbsence &absence,
			const QStringList &keys) {
		auto result = !absence.certified
			&& (absence.uncovered.size() == keys.size());
		for (auto i = 0; result && (i != int(keys.size())); ++i) {
			result = absence.uncovered[i].startsWith(keys[i] + u": "_q);
		}
		return result;
	};
	const auto passed = named(absences.leaks, { x, y })
		&& named(absences.gaps, { x, y })
		&& named(absences.telegramLinks, { y });
	return SelfCheckRow(
		passed,
		u"totals self-check: with the scan's control leaks matched, selected "
			u"runs no tap was armed for leave the leak and gap absences "
			u"uncertified, and the one without a bridge reading the "
			u"openTelegramLink absence, each named"_q,
		CompactJson(AbsencesObject(absences)));
}

// The copied --attacks scanner's leak count over every run, certified only
// when the same scan matched the self-check's control leaks.
[[nodiscard]] bool ReportScan(const ConformanceState &state) {
	auto scan = DiscriminatingScan(
		Prefixed(u"validated-ops leak scan"_q),
		u"conformance widget leak"_q,
		u"self-check control leak"_q);
	scan.examined(state.control.batches);
	const auto &control = state.control;
	for (auto i = 0; i != control.leaks; ++i) {
		scan.matchedControl((i < int(control.leakExamples.size()))
			? QString::fromStdString(control.leakExamples[i])
			: QString());
	}
	for (const auto &run : state.runs) {
		if (!run.message || !run.message->tap) {
			continue;
		}
		const auto &reading = run.reading;
		scan.examined(reading.batches);
		for (auto i = 0; i != reading.leaks; ++i) {
			scan.matchedSubject(run.key
				+ u": "_q
				+ ((i < int(reading.leakExamples.size()))
					? QString::fromStdString(reading.leakExamples[i])
					: u"(example not kept)"_q));
		}
	}
	return scan.report();
}

// The judgements of the totals stage, from the tallies.
void CheckTallies(const ConformanceState &state, const Tallies &tallies) {
	const auto selected = int(state.runs.size());
	Check(
		selected && (tallies.completed == selected),
		Prefixed(u"every selected run completed"_q),
		u"selected=%1 completed=%2 incomplete=[%3]"_q.arg(
			NumberText(selected),
			NumberText(tallies.completed),
			tallies.incomplete.join(u"; "_q)));
	Check(
		tallies.unfocused.isEmpty(),
		Prefixed(u"every completed run kept the chat's input field focused "
			u"through its load (chatFocused)"_q),
		u"focused=%1 not=[%2]"_q.arg(
			NumberText(tallies.focused),
			tallies.unfocused.join(u"; "_q)));
	Check(
		tallies.refused.isEmpty(),
		Prefixed(u"no completed run carries a runner refusal"_q),
		u"refusals=%1 [%2]"_q.arg(
			CompactJson(tallies.refusals),
			tallies.refused.join(u"; "_q)));
	Check(
		tallies.missingActions.isEmpty(),
		Prefixed(u"every scripted action of every completed run was "
			u"performed with real input, or skipped as one the desktop "
			u"cannot perform"_q),
		u"actions=%1 performed=%2 skipped=%3 missing=[%4]"_q.arg(
			NumberText(tallies.actions),
			NumberText(tallies.performed),
			NumberText(tallies.skipped),
			tallies.missingActions.join(u"; "_q)));
	Check(
		tallies.missingCrops.isEmpty(),
		Prefixed(u"every requested stage crop of every completed run was "
			u"saved"_q),
		u"crops=%1 saved=%2 missing=[%3]"_q.arg(
			NumberText(tallies.crops),
			NumberText(tallies.saved),
			tallies.missingCrops.join(u"; "_q)));
	CheckPictures(state);
	// WHY: the scan's control leaks prove it can match, not that it saw
	// the widgets: an absence holds only over runs whose readings cover it,
	// every selected one, as JudgeConformanceAbsences() requires.
	const auto discriminates = ReportScan(state);
	const auto &absences = tallies.absences;
	Check(
		discriminates && absences.leaks.certified,
		Prefixed(u"no conformance widget leaks a URL into the validated "
			u"ops"_q),
		u"leaks=%1 runs=[%2] batches=%3 control=%4 %5"_q.arg(
			NumberText(tallies.leaks),
			tallies.leaking.join(u","_q),
			NumberText(tallies.batches),
			NumberText(state.control.leaks),
			AbsenceText(absences.leaks)));
	Check(
		absences.gaps.certified,
		Prefixed(u"every host message of every run was scanned: no scan "
			u"gaps"_q),
		u"gaps=%1 runs=[%2] %3"_q.arg(
			NumberText(tallies.gaps),
			tallies.gapped.join(u","_q),
			AbsenceText(absences.gaps)));
	Check(
		tallies.unfinished.isEmpty() && state.abort.isEmpty(),
		Prefixed(u"every run's factories and wrappers finished before its "
			u"final reading"_q),
		u"unfinished=[%1] abort=[%2]"_q.arg(
			tallies.unfinished.join(u","_q),
			state.abort));
	Check(
		absences.telegramLinks.certified,
		Prefixed(u"no host accepted an openTelegramLink call: no scripted "
			u"action asks for one, and it would leave the bubble through the "
			u"app's t.me route"_q),
		u"accepted=[%1] %2"_q.arg(
			tallies.telegramLinks.join(u"; "_q),
			AbsenceText(absences.telegramLinks)));
	Note(Prefixed(u"recorded, not failed: dead=[%1] notLoaded=[%2] "
		u"noWantedStatusIn20s=[%3] noDoneIn10sAfterActions=[%4] "
		u"consoleLimits=[%5]"_q.arg(
			tallies.dead.join(u"; "_q),
			tallies.notLoaded.join(u","_q),
			tallies.bound.join(u","_q),
			tallies.doneBound.join(u","_q),
			tallies.consoleLimited.join(u"; "_q))));
	Note(Prefixed(u"layers shown over the runs' messages, each hidden at "
		u"once with nothing in it confirmed: %1 [%2]"_q.arg(
			NumberText(tallies.layers),
			tallies.layered.join(u"; "_q))));
	CheckNoBlockedLaunches(Prefixed(u"no widget handed anything to the "
		u"operating system"_q));
}

// Writes the run file and the totals row. Only the totals stage decides
// completion and the rest as well: a run file written from
// Runner::onFinish is of a launch that already failed or was cut.
void WriteTotals(ConformanceState &state, const QString &path) {
	const auto written = SaveRunFile(state, path);
	const auto tallies = CountTallies(state);
	Row(u"CONFORMANCE_TOTALS"_q, {
		{ u"path"_q, path },
		{ u"selected"_q, int(state.runs.size()) },
		{ u"completed"_q, tallies.completed },
		{ u"incomplete"_q, Strings(tallies.incomplete) },
		{ u"omitted"_q, int(state.omitted.size()) },
		{ u"loaded"_q, tallies.loaded },
		{ u"notLoaded"_q, Strings(tallies.notLoaded) },
		{ u"dead"_q, Strings(tallies.dead) },
		{ u"noWantedStatus"_q, Strings(tallies.bound) },
		{ u"noDoneAfterActions"_q, Strings(tallies.doneBound) },
		{ u"consoleLimits"_q, Strings(tallies.consoleLimited) },
		{ u"chatFocused"_q, tallies.focused },
		{ u"notChatFocused"_q, Strings(tallies.unfocused) },
		{ u"refusals"_q, tallies.refusals },
		{ u"refused"_q, Strings(tallies.refused) },
		{ u"actions"_q, tallies.actions },
		{ u"actionsPerformed"_q, tallies.performed },
		{ u"actionsSkipped"_q, tallies.skipped },
		{ u"actionsMissing"_q, Strings(tallies.missingActions) },
		{ u"crops"_q, tallies.crops },
		{ u"cropsSaved"_q, tallies.saved },
		{ u"cropsMissing"_q, Strings(tallies.missingCrops) },
		{ u"pictures"_q, PictureTotals(state) },
		{ u"bridgeObserved"_q, tallies.bridgeObserved },
		{ u"leaks"_q, tallies.leaks },
		{ u"leaking"_q, Strings(tallies.leaking) },
		{ u"notes"_q, tallies.notes },
		{ u"gaps"_q, tallies.gaps },
		{ u"violations"_q, tallies.violations },
		{ u"batches"_q, tallies.batches },
		{ u"unfinished"_q, Strings(tallies.unfinished) },
		{ u"telegramLinks"_q, Strings(tallies.telegramLinks) },
		{ u"absences"_q, AbsencesObject(tallies.absences) },
		{ u"layers"_q, tallies.layers },
		{ u"layered"_q, Strings(tallies.layered) },
		{ u"abort"_q, state.abort },
		{ u"unexpectedOutgoing"_q, int(state.outgoing.size()) },
		{ u"palette"_q, PaletteArray(state) },
		{ u"selfCheckRows"_q, state.selfCheckRows },
		{ u"selfCheckFailed"_q, Strings(state.selfCheckFailed) },
		{ u"written"_q, written },
		{ u"file"_q, state.runFile },
		{ u"bytes"_q, state.writtenBytes },
		{ u"outputError"_q, state.outputError },
		{ u"error"_q, state.saveError },
	});
	Check(
		written,
		Prefixed(u"the run file is written (%1)"_q.arg(path)),
		written
			? u"%1 bytes=%2"_q.arg(
				state.runFile,
				NumberText(state.writtenBytes))
			: state.saveError);
	if (path == u"totals"_q) {
		CheckTallies(state, tallies);
	}
}

} // namespace

} // namespace Test::Conformance

namespace Test {

void AppendWidgetConformance(
		not_null<Runner*> runner,
		WidgetConformanceArgs args) {
	using namespace Conformance;

	const auto state = std::make_shared<ConformanceState>();
	state->args = std::move(args);
	state->date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	state->fixture = MakeWidgetFixture({
		.chat = state->args.chat,
		.prefix = QString::fromLatin1(kPrefix),
		.marker = u"CONFORMANCE"_q,
		.token = state->args.token,
		.ports = state->args.ports,
	});
	ReadPackage(*state);
	SelectRuns(*state);
	SelectPictures(*state);

	// Registered before anything is changed: every path that reaches
	// Runner::finish() stops the watcher, ends a pointer sequence still
	// running, keeps the taps' last readings, disarms every played bot by
	// its run's kept key (their records stay with the runs), runs the same
	// idempotent release, then writes the run file when the totals stage
	// did not, so no selected run goes unreported.
	runner->onFinish([=] {
		StopWatcher(*state);
		AbortRunActions(*state, u"onFinish"_q);
		SaveReadings(*state);
		for (const auto &run : state->runs) {
			if (run.bot) {
				DisarmWidgetBot(run.message->key);
			}
		}
		const auto release = ReleaseWidgetFixture(
			*state->fixture,
			u"onFinish"_q);
		Note(Prefixed(u"onFinish release: "_q) + release);
		if (!state->totalsDone) {
			state->totalsDone = true;
			WriteTotals(*state, u"onFinish"_q);
		}
	});

	runner->add({
		.name = Prefixed(u"tap, result reader, summary reader and run file "
			u"self-check"_q),
		.run = [=] {
			RunSelfChecks(*state);
		},
	});
	runner->add({
		.name = Prefixed(u"report the package, the selection and the "
			u"output"_q),
		.run = [=] {
			ReportPackage(*state);
		},
	});
	AppendWidgetFixtureOpen(runner, state->fixture);
	AppendWidgetFixtureOneColumn(runner, state->fixture);
	runner->add({
		.name = Prefixed(u"watch the session for outgoing messages"_q),
		.skipReason = [=] {
			return GlobalSkip(*state);
		},
		.run = [=] {
			StartWatcher(state);
		},
	});
	AppendWidgetFixtureControls(
		runner,
		state->fixture,
		WidgetFixtureControls::Before,
		u"before the first widget"_q);

	// Light runs first, then dark: one palette phase per theme.
	auto theme = QString();
	for (auto i = 0; i != int(state->runs.size()); ++i) {
		if (state->runs[i].theme != theme) {
			theme = state->runs[i].theme;
			AppendPalettePhase(runner, state, theme);
		}
		state->runs[i].palette = int(state->palette.size()) - 1;
		AppendRunFillers(runner, state, i);
		AppendRunStart(runner, state, i);
		AppendRunActions(runner, state, i);
		AppendDoneWait(runner, state, i);
		AppendSnapshot(runner, state, i);
		AppendPictureBands(runner, state, i);
		AppendRunEnd(runner, state, i);
	}
	AppendWidgetFixtureControls(
		runner,
		state->fixture,
		WidgetFixtureControls::After,
		u"after the last widget"_q);
	AppendPaletteEnd(runner, state);

	runner->add({
		.name = Prefixed(u"stop watching for outgoing messages"_q),
		.skipReason = [=] {
			return state->watching
				? QString()
				: u"the watcher was never installed"_q;
		},
		.run = [=] {
			StopWatcher(*state);
			CheckOutgoing(*state);
		},
	});
	runner->add({
		.name = Prefixed(u"totals and the run file"_q),
		.run = [=] {
			state->totalsDone = true;
			WriteTotals(*state, u"totals"_q);
		},
	});
	AppendWidgetFixtureClose(runner, state->fixture);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
