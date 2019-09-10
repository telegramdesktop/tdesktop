/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_sets_manager.h"

#include "mtproto/dedicated_file_loader.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "base/zlib_help.h"
#include "layout.h"
#include "core/application.h"
#include "main/main_account.h"
#include "mainwidget.h"
#include "app.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace Emoji {
namespace {

struct Available {
	int size = 0;

	inline bool operator<(const Available &other) const {
		return size < other.size;
	}
	inline bool operator==(const Available &other) const {
		return size == other.size;
	}
};
struct Ready {
	inline bool operator<(const Ready &other) const {
		return false;
	}
	inline bool operator==(const Ready &other) const {
		return true;
	}
};
struct Active {
	inline bool operator<(const Active &other) const {
		return false;
	}
	inline bool operator==(const Active &other) const {
		return true;
	}
};
using Loading = MTP::DedicatedLoader::Progress;
struct Failed {
	inline bool operator<(const Failed &other) const {
		return false;
	}
	inline bool operator==(const Failed &other) const {
		return true;
	}
};
using SetState = base::variant<
	Available,
	Ready,
	Active,
	Loading,
	Failed>;

class Loader : public QObject {
public:
	Loader(QObject *parent, int id);

	int id() const;

	rpl::producer<SetState> state() const;
	void destroy();

private:
	void setImplementation(std::unique_ptr<MTP::DedicatedLoader> loader);
	void unpack(const QString &path);
	void finalize(const QString &path);
	void fail();

	int _id = 0;
	int _size = 0;
	rpl::variable<SetState> _state;

	MTP::WeakInstance _mtproto;
	std::unique_ptr<MTP::DedicatedLoader> _implementation;

};

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent);

private:
	void setupContent();

};

class Row : public Ui::RippleButton {
public:
	Row(QWidget *widget, const Set &set);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	[[nodiscard]] bool showOver() const;
	[[nodiscard]] bool showOver(State state) const;
	void updateStatusColorOverride();
	void setupContent(const Set &set);
	void setupLabels(const Set &set);
	void setupPreview(const Set &set);
	void setupAnimation();
	void paintPreview(Painter &p) const;
	void paintRadio(Painter &p);
	void setupHandler();
	void load();
	void radialAnimationCallback(crl::time now);

	int _id = 0;
	bool _switching = false;
	rpl::variable<SetState> _state;
	Ui::FlatLabel *_status = nullptr;
	std::array<QPixmap, 4> _preview;
	Ui::Animations::Simple _toggled;
	Ui::Animations::Simple _active;
	std::unique_ptr<Ui::RadialAnimation> _loading;

};

base::unique_qptr<Loader> GlobalLoader;
rpl::event_stream<Loader*> GlobalLoaderValues;

void SetGlobalLoader(base::unique_qptr<Loader> loader) {
	GlobalLoader = std::move(loader);
	GlobalLoaderValues.fire(GlobalLoader.get());
}

int GetDownloadSize(int id) {
	const auto sets = Sets();
	return ranges::find(sets, id, &Set::id)->size;
}

MTP::DedicatedLoader::Location GetDownloadLocation(int id) {
	constexpr auto kUsername = "tdhbcfiles";
	const auto sets = Sets();
	const auto i = ranges::find(sets, id, &Set::id);
	return MTP::DedicatedLoader::Location{ kUsername, i->postId };
}

SetState ComputeState(int id) {
	if (id == CurrentSetId()) {
		return Active();
	} else if (SetIsReady(id)) {
		return Ready();
	}
	return Available{ GetDownloadSize(id) };
}

QString StateDescription(const SetState &state) {
	return state.match([](const Available &data) {
		return tr::lng_emoji_set_download(tr::now, lt_size, formatSizeText(data.size));
	}, [](const Ready &data) -> QString {
		return tr::lng_emoji_set_ready(tr::now);
	}, [](const Active &data) -> QString {
		return tr::lng_emoji_set_active(tr::now);
	}, [](const Loading &data) {
		const auto percent = (data.size > 0)
			? snap((data.already * 100) / float64(data.size), 0., 100.)
			: 0.;
		return tr::lng_emoji_set_loading(
			tr::now,
			lt_percent,
			QString::number(int(std::round(percent))) + '%',
			lt_progress,
			formatDownloadText(data.already, data.size));
	}, [](const Failed &data) {
		return tr::lng_attach_failed(tr::now);
	});
}

QByteArray ReadFinalFile(const QString &path) {
	constexpr auto kMaxZipSize = 10 * 1024 * 1024;
	auto file = QFile(path);
	if (file.size() > kMaxZipSize || !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

bool ExtractZipFile(zlib::FileToRead &zip, const QString path) {
	constexpr auto kMaxSize = 10 * 1024 * 1024;
	const auto content = zip.readCurrentFileContent(kMaxSize);
	if (content.isEmpty() || zip.error() != UNZ_OK) {
		return false;
	}
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size());
}

bool GoodSetPartName(const QString &name) {
	return (name == qstr("config.json"))
		|| (name.startsWith(qstr("emoji_"))
			&& name.endsWith(qstr(".webp")));
}

bool UnpackSet(const QString &path, const QString &folder) {
	const auto bytes = ReadFinalFile(path);
	if (bytes.isEmpty()) {
		return false;
	}

	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		return false;
	}
	do {
		const auto name = zip.getCurrentFileName();
		const auto path = folder + '/' + name;
		if (GoodSetPartName(name) && !ExtractZipFile(zip, path)) {
			return false;
		}

		const auto jump = zip.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			return false;
		}
	} while (true);
	return true;
}

Loader::Loader(QObject *parent, int id)
: QObject(parent)
, _id(id)
, _size(GetDownloadSize(_id))
, _state(Loading{ 0, _size })
, _mtproto(Core::App().activeAccount().mtp()) {
	const auto ready = [=](std::unique_ptr<MTP::DedicatedLoader> loader) {
		if (loader) {
			setImplementation(std::move(loader));
		} else {
			fail();
		}
	};
	const auto location = GetDownloadLocation(id);
	const auto folder = internal::SetDataPath(id);
	MTP::StartDedicatedLoader(&_mtproto, location, folder, ready);
}

int Loader::id() const {
	return _id;
}

rpl::producer<SetState> Loader::state() const {
	return _state.value();
}

void Loader::setImplementation(
		std::unique_ptr<MTP::DedicatedLoader> loader) {
	_implementation = std::move(loader);
	auto convert = [](auto value) {
		return SetState(value);
	};
	_state = _implementation->progress(
	) | rpl::map([](const Loading &state) {
		return SetState(state);
	});
	_implementation->failed(
	) | rpl::start_with_next([=] {
		fail();
	}, _implementation->lifetime());

	_implementation->ready(
	) | rpl::start_with_next([=](const QString &filepath) {
		unpack(filepath);
	}, _implementation->lifetime());

	QDir(internal::SetDataPath(_id)).removeRecursively();
	_implementation->start();
}

void Loader::unpack(const QString &path) {
	const auto folder = internal::SetDataPath(_id);
	const auto weak = Ui::MakeWeak(this);
	crl::async([=] {
		if (UnpackSet(path, folder)) {
			QFile(path).remove();
			SwitchToSet(_id, crl::guard(weak, [=](bool success) {
				if (success) {
					destroy();
				} else {
					fail();
				}
			}));
		} else {
			crl::on_main(weak, [=] {
				fail();
			});
		}
	});
}

void Loader::finalize(const QString &path) {
}

void Loader::fail() {
	_state = Failed();
}

void Loader::destroy() {
	Expects(GlobalLoader == this);

	SetGlobalLoader(nullptr);
}

Inner::Inner(QWidget *parent) : RpWidget(parent) {
	setupContent();
}

void Inner::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto sets = Sets();
	for (const auto &set : sets) {
		content->add(object_ptr<Row>(content, set));
	}

	content->resizeToWidth(st::boxWidth);
	Ui::ResizeFitChild(this, content);
}

Row::Row(QWidget *widget, const Set &set)
: RippleButton(widget, st::contactsRipple)
, _id(set.id)
, _state(Available{ set.size }) {
	setupContent(set);
	setupHandler();
}

void Row::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto over = showOver();
	const auto bg = over ? st::windowBgOver : st::windowBg;
	p.fillRect(rect(), bg);

	paintRipple(p, 0, 0);
	paintPreview(p);
	paintRadio(p);
}

void Row::paintPreview(Painter &p) const {
	const auto x = st::manageEmojiPreviewPadding.left();
	const auto y = st::manageEmojiPreviewPadding.top();
	const auto width = st::manageEmojiPreviewWidth;
	const auto height = st::manageEmojiPreviewWidth;
	auto &&preview = ranges::view::zip(_preview, ranges::view::ints(0, int(_preview.size())));
	for (const auto &[pixmap, index] : preview) {
		const auto row = (index / 2);
		const auto column = (index % 2);
		const auto left = x + (column ? width - st::manageEmojiPreview : 0);
		const auto top = y + (row ? height - st::manageEmojiPreview : 0);
		p.drawPixmap(left, top, pixmap);
	}
}

void Row::paintRadio(Painter &p) {
	const auto loading = _loading
		? _loading->computeState()
		: Ui::RadialState{ 0., 0, FullArcLength };
	const auto isToggledSet = _state.current().is<Active>();
	const auto isActiveSet = isToggledSet || _state.current().is<Loading>();
	const auto toggled = _toggled.value(isToggledSet ? 1. : 0.);
	const auto active = _active.value(isActiveSet ? 1. : 0.);
	const auto _st = &st::defaultRadio;

	PainterHighQualityEnabler hq(p);

	const auto left = width()
		- st::manageEmojiMarginRight
		- _st->diameter
		- _st->thickness;
	const auto top = (height() - _st->diameter - _st->thickness) / 2;
	const auto outerWidth = width();

	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, active);
	pen.setWidth(_st->thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(_st->bg);
	const auto rect = style::rtlrect(QRectF(
		left,
		top,
		_st->diameter,
		_st->diameter
	).marginsRemoved(QMarginsF(
		_st->thickness / 2.,
		_st->thickness / 2.,
		_st->thickness / 2.,
		_st->thickness / 2.
	)), outerWidth);
	if (loading.shown > 0 && anim::Disabled()) {
		anim::DrawStaticLoading(
			p,
			rect,
			_st->thickness,
			pen.color(),
			_st->bg);
	} else if (loading.arcLength < FullArcLength) {
		p.drawArc(rect, loading.arcFrom, loading.arcLength);
	} else {
		p.drawEllipse(rect);
	}

	if (toggled > 0 && (!_loading || !anim::Disabled())) {
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(_st->untoggledFg, _st->toggledFg, toggled));

		const auto skip0 = _st->diameter / 2.;
		const auto skip1 = _st->skip / 10.;
		const auto checkSkip = skip0 * (1. - toggled) + skip1 * toggled;
		p.drawEllipse(style::rtlrect(QRectF(
			left,
			top,
			_st->diameter,
			_st->diameter
		).marginsRemoved(QMarginsF(
			checkSkip,
			checkSkip,
			checkSkip,
			checkSkip
		)), outerWidth));
	}
}

bool Row::showOver(State state) const {
	return (!(state & StateFlag::Disabled))
		&& (state & (StateFlag::Over | StateFlag::Down));
}

bool Row::showOver() const {
	return showOver(state());
}

void Row::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if (showOver() != showOver(was)) {
		updateStatusColorOverride();
	}
}

void Row::updateStatusColorOverride() {
	const auto isToggledSet = _state.current().is<Active>();
	const auto toggled = _toggled.value(isToggledSet ? 1. : 0.);
	const auto over = showOver();
	if (toggled == 0. && !over) {
		_status->setTextColorOverride(std::nullopt);
	} else {
		_status->setTextColorOverride(anim::color(
			over ? st::contactsStatusFgOver : st::contactsStatusFg,
			st::contactsStatusFgOnline,
			toggled));
	}
}

void Row::setupContent(const Set &set) {
	_state = GlobalLoaderValues.events_starting_with(
		GlobalLoader.get()
	) | rpl::map([=](Loader *loader) {
		return (loader && loader->id() == _id)
			? loader->state()
			: rpl::single(
				rpl::empty_value()
			) | rpl::then(
				Updated()
			) | rpl::map([=] {
				return ComputeState(_id);
			});
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const SetState &state) {
		return !_state.current().is<Failed>() || !state.is<Available>();
	});

	setupLabels(set);
	setupPreview(set);
	setupAnimation();

	const auto height = st::manageEmojiPreviewPadding.top()
		+ st::manageEmojiPreviewHeight
		+ st::manageEmojiPreviewPadding.bottom();
	resize(width(), height);
}

void Row::setupHandler() {
	clicks(
	) | rpl::filter([=] {
		const auto &state = _state.current();
		return !_switching && (state.is<Ready>() || state.is<Available>());
	}) | rpl::start_with_next([=] {
		if (_state.current().is<Available>()) {
			load();
			return;
		}
		_switching = true;
		SwitchToSet(_id, crl::guard(this, [=](bool success) {
			_switching = false;
			if (!success) {
				load();
			} else if (GlobalLoader && GlobalLoader->id() == _id) {
				GlobalLoader->destroy();
			}
		}));
	}, lifetime());

	_state.value(
	) | rpl::map([=](const SetState &state) {
		return state.is<Ready>() || state.is<Available>();
	}) | rpl::start_with_next([=](bool active) {
		setDisabled(!active);
		setPointerCursor(active);
	}, lifetime());
}

void Row::load() {
	SetGlobalLoader(base::make_unique_q<Loader>(App::main(), _id));
}

void Row::setupLabels(const Set &set) {
	using namespace rpl::mappers;

	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		this,
		set.name,
		st::localStorageRowTitle);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status = Ui::CreateChild<Ui::FlatLabel>(
		this,
		_state.value() | rpl::map(StateDescription),
		st::localStorageRowSize);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto left = st::manageEmojiPreviewPadding.left()
			+ st::manageEmojiPreviewWidth
			+ st::manageEmojiPreviewPadding.right();
		const auto namey = st::manageEmojiPreviewPadding.top()
			+ st::manageEmojiNameTop;
		const auto statusy = st::manageEmojiPreviewPadding.top()
			+ st::manageEmojiStatusTop;
		name->moveToLeft(left, namey);
		_status->moveToLeft(left, statusy);
	}, name->lifetime());
}

void Row::setupPreview(const Set &set) {
	const auto size = st::manageEmojiPreview * cIntRetinaFactor();
	const auto original = QImage(set.previewPath);
	const auto full = original.height();
	auto &&preview = ranges::view::zip(_preview, ranges::view::ints(0, int(_preview.size())));
	for (auto &&[pixmap, index] : preview) {
		pixmap = App::pixmapFromImageInPlace(original.copy(
			{ full * index, 0, full, full }
		).scaledToWidth(size, Qt::SmoothTransformation));
		pixmap.setDevicePixelRatio(cRetinaFactor());
	}
}

void Row::radialAnimationCallback(crl::time now) {
	const auto updated = [&] {
		const auto state = _state.current();
		if (const auto loading = base::get_if<Loading>(&state)) {
			const auto progress = (loading->size > 0)
				? (loading->already / float64(loading->size))
				: 0.;
			return _loading->update(progress, false, now);
		}
		return false;
	}();
	if (!anim::Disabled() || updated) {
		update();
	}
}

void Row::setupAnimation() {
	using namespace rpl::mappers;

	_state.value(
	) | rpl::start_with_next([=](const SetState &state) {
		update();
	}, lifetime());

	_state.value(
	) | rpl::map(
		_1 == Active()
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool toggled) {
		_toggled.start(
			[=] { updateStatusColorOverride(); update(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::defaultRadio.duration);
	}, lifetime());

	_state.value(
	) | rpl::map([](const SetState &state) {
		return state.is<Loading>() || state.is<Active>();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool active) {
		_active.start(
			[=] { update(); },
			active ? 0. : 1.,
			active ? 1. : 0.,
			st::defaultRadio.duration);
	}, lifetime());

	_state.value(
	) | rpl::map([](const SetState &state) {
		return base::get_if<Loading>(&state);
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](const Loading *loading) {
		if (loading && !_loading) {
			_loading = std::make_unique<Ui::RadialAnimation>(
				[=](crl::time now) { radialAnimationCallback(now); });
			const auto progress = (loading->size > 0)
				? (loading->already / float64(loading->size))
				: 0.;
			_loading->start(progress);
		} else if (!loading && _loading) {
			_loading->update(
				_state.current().is<Failed>() ? 0. : 1.,
				true,
				crl::now());
		}
	}, lifetime());

	_toggled.stop();
	_active.stop();
	updateStatusColorOverride();
}

} // namespace

ManageSetsBox::ManageSetsBox(QWidget*) {
}

void ManageSetsBox::prepare() {
	const auto inner = setInnerWidget(object_ptr<Inner>(this));

	setTitle(tr::lng_emoji_manage_sets());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, inner);
}

} // namespace Emoji
} // namespace Ui
