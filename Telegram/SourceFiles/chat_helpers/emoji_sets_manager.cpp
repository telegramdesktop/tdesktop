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
#include "core/application.h"
#include "main/main_account.h"
#include "mainwidget.h"
#include "app.h"
#include "storage/storage_cloud_blob.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace Emoji {
namespace {

using namespace Storage::CloudBlob;

struct Set : public Blob {
	QString previewPath;
};

inline auto PreviewPath(int i) {
	return qsl(":/gui/emoji/set%1_preview.webp").arg(i);
}

const auto kSets = {
	Set{ {0,   0,         0, "Mac"},       PreviewPath(0) },
	Set{ {1, 713, 7'313'166, "Android"},   PreviewPath(1) },
	Set{ {2, 714, 4'690'333, "Twemoji"},   PreviewPath(2) },
	Set{ {3, 716, 5'968'021, "JoyPixels"}, PreviewPath(3) },
};

using Loading = MTP::DedicatedLoader::Progress;
using SetState = BlobState;

class Loader final : public BlobLoader {
public:
	Loader(
		not_null<Main::Session*> session,
		int id,
		MTP::DedicatedLoader::Location location,
		const QString &folder,
		int size);

	void destroy() override;
	void unpack(const QString &path) override;

private:
	void fail() override;

};

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent, not_null<Main::Session*> session);

private:
	void setupContent();

	const not_null<Main::Session*> _session;

};

class Row : public Ui::RippleButton {
public:
	Row(QWidget *widget, not_null<Main::Session*> session, const Set &set);

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
	void updateLoadingToFinished();

	const not_null<Main::Session*> _session;
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
	return ranges::find(kSets, id, &Set::id)->size;
}

[[nodiscard]] float64 CountProgress(not_null<const Loading*> loading) {
	return (loading->size > 0)
		? (loading->already / float64(loading->size))
		: 0.;
}

MTP::DedicatedLoader::Location GetDownloadLocation(int id) {
	const auto username = kCloudLocationUsername.utf16();
	const auto i = ranges::find(kSets, id, &Set::id);
	return MTP::DedicatedLoader::Location{ username, i->postId };
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
	return StateDescription(
		state,
		tr::lng_emoji_set_active);
}

bool GoodSetPartName(const QString &name) {
	return (name == qstr("config.json"))
		|| (name.startsWith(qstr("emoji_"))
			&& name.endsWith(qstr(".webp")));
}

bool UnpackSet(const QString &path, const QString &folder) {
	return UnpackBlob(path, folder, GoodSetPartName);
}


Loader::Loader(
	not_null<Main::Session*> session,
	int id,
	MTP::DedicatedLoader::Location location,
	const QString &folder,
	int size)
: BlobLoader(nullptr, session, id, location, folder, size) {
}

void Loader::unpack(const QString &path) {
	const auto folder = internal::SetDataPath(id());
	const auto weak = Ui::MakeWeak(this);
	crl::async([=] {
		if (UnpackSet(path, folder)) {
			QFile(path).remove();
			SwitchToSet(id(), crl::guard(weak, [=](bool success) {
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

void Loader::destroy() {
	Expects(GlobalLoader == this);

	SetGlobalLoader(nullptr);
}

void Loader::fail() {
	ClearNeedSwitchToId();
	BlobLoader::fail();
}

Inner::Inner(QWidget *parent, not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session) {
	setupContent();
}

void Inner::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	for (const auto &set : kSets) {
		content->add(object_ptr<Row>(content, _session, set));
	}

	content->resizeToWidth(st::boxWidth);
	Ui::ResizeFitChild(this, content);
}

Row::Row(QWidget *widget, not_null<Main::Session*> session, const Set &set)
: RippleButton(widget, st::defaultRippleAnimation)
, _session(session)
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
	auto &&preview = ranges::views::zip(_preview, ranges::views::ints(0, int(_preview.size())));
	for (const auto &[pixmap, index] : preview) {
		const auto row = (index / 2);
		const auto column = (index % 2);
		const auto left = x + (column ? width - st::manageEmojiPreview : 0);
		const auto top = y + (row ? height - st::manageEmojiPreview : 0);
		p.drawPixmap(left, top, pixmap);
	}
}

void Row::paintRadio(Painter &p) {
	if (_loading && !_loading->animating()) {
		_loading = nullptr;
	}
	const auto loading = _loading
		? _loading->computeState()
		: Ui::RadialState{ 0., 0, FullArcLength };
	const auto isToggledSet = v::is<Active>(_state.current());
	const auto isActiveSet = isToggledSet || v::is<Loading>(_state.current());
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
	const auto isToggledSet = v::is<Active>(_state.current());
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
		return !v::is<Failed>(_state.current())
			|| !v::is<Available>(state);
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
		return !_switching && (v::is<Ready>(state)
			|| v::is<Available>(state));
	}) | rpl::start_with_next([=] {
		if (v::is<Available>(_state.current())) {
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
		return v::is<Ready>(state) || v::is<Available>(state);
	}) | rpl::start_with_next([=](bool active) {
		setDisabled(!active);
		setPointerCursor(active);
	}, lifetime());
}

void Row::load() {
	LoadAndSwitchTo(_session, _id);
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
	auto &&preview = ranges::views::zip(_preview, ranges::views::ints(0, int(_preview.size())));
	for (auto &&[pixmap, index] : preview) {
		pixmap = App::pixmapFromImageInPlace(original.copy(
			{ full * index, 0, full, full }
		).scaledToWidth(size, Qt::SmoothTransformation));
		pixmap.setDevicePixelRatio(cRetinaFactor());
	}
}

void Row::updateLoadingToFinished() {
	_loading->update(
		v::is<Failed>(_state.current()) ? 0. : 1.,
		true,
		crl::now());
}

void Row::radialAnimationCallback(crl::time now) {
	const auto updated = [&] {
		const auto state = _state.current();
		if (const auto loading = std::get_if<Loading>(&state)) {
			return _loading->update(CountProgress(loading), false, now);
		} else {
			updateLoadingToFinished();
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
		_1 == SetState{ Active() }
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
		return v::is<Loading>(state) || v::is<Active>(state);
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
		return std::get_if<Loading>(&state);
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](const Loading *loading) {
		if (loading && !_loading) {
			_loading = std::make_unique<Ui::RadialAnimation>(
				[=](crl::time now) { radialAnimationCallback(now); });
			_loading->start(CountProgress(loading));
		} else if (!loading && _loading) {
			updateLoadingToFinished();
		}
	}, lifetime());

	_toggled.stop();
	_active.stop();
	updateStatusColorOverride();
}

} // namespace

ManageSetsBox::ManageSetsBox(QWidget*, not_null<Main::Session*> session)
: _session(session) {
}

void ManageSetsBox::prepare() {
	const auto inner = setInnerWidget(object_ptr<Inner>(this, _session));

	setTitle(tr::lng_emoji_manage_sets());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, inner);
}

void LoadAndSwitchTo(not_null<Main::Session*> session, int id) {
	if (!ranges::contains(kSets, id, &Set::id)) {
		ClearNeedSwitchToId();
		return;
	}
	SetGlobalLoader(base::make_unique_q<Loader>(
		session,
		id,
		GetDownloadLocation(id),
		internal::SetDataPath(id),
		GetDownloadSize(id)));
}

} // namespace Emoji
} // namespace Ui
