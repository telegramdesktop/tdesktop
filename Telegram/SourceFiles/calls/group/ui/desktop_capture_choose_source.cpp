/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/ui/desktop_capture_choose_source.h"

#include "ui/widgets/window.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/platform/ui_platform_window_title.h"
#include "base/platform/base_platform_info.h"
#include "webrtc/webrtc_video_track.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"

#include <tgcalls/desktop_capturer/DesktopCaptureSourceManager.h>
#include <tgcalls/desktop_capturer/DesktopCaptureSourceHelper.h>
#include <QtGui/QWindow>

namespace Calls::Group::Ui::DesktopCapture {
namespace {

constexpr auto kColumns = 3;
constexpr auto kRows = 2;

struct Preview {
	explicit Preview(tgcalls::DesktopCaptureSource source);

	tgcalls::DesktopCaptureSourceHelper helper;
	Webrtc::VideoTrack track;
	rpl::lifetime lifetime;
};

class SourceButton final : public RippleButton {
public:
	using RippleButton::RippleButton;

private:
	QImage prepareRippleMask() const override;

};

QImage SourceButton::prepareRippleMask() const {
	return RippleAnimation::roundRectMask(size(), st::roundRadiusLarge);
}

class Source final {
public:
	Source(
		not_null<QWidget*> parent,
		tgcalls::DesktopCaptureSource source,
		const QString &title);

	void setGeometry(QRect geometry);
	void clearHelper();

	[[nodiscard]] rpl::producer<> activations() const;
	void setActive(bool active);
	[[nodiscard]] bool isWindow() const;
	[[nodiscard]] QString deviceIdKey() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void paint();
	void setupPreview();

	SourceButton _widget;
	FlatLabel _label;
	RoundRect _selectedRect;
	RoundRect _activeRect;
	tgcalls::DesktopCaptureSource _source;
	std::unique_ptr<Preview> _preview;
	rpl::event_stream<> _activations;
	QImage _frame;
	bool _active = false;

};

class ChooseSourceProcess final {
public:
	static void Start(not_null<ChooseSourceDelegate*> delegate);

	explicit ChooseSourceProcess(not_null<ChooseSourceDelegate*> delegate);

	void activate();

private:
	void setupPanel();
	void setupSources();
	void setupGeometryWithParent(not_null<QWidget*> parent);
	void fillSources();
	void setupSourcesGeometry();
	void updateButtonsVisibility();
	void destroy();

	static base::flat_map<
		not_null<ChooseSourceDelegate*>,
		std::unique_ptr<ChooseSourceProcess>> &Map();

	const not_null<ChooseSourceDelegate*> _delegate;
	const std::unique_ptr<Ui::Window> _window;
	const std::unique_ptr<ScrollArea> _scroll;
	const not_null<RpWidget*> _inner;
	const not_null<RpWidget*> _bottom;
	const not_null<RoundButton*> _submit;
	const not_null<RoundButton*> _finish;
	const not_null<Checkbox*> _withAudio;

	std::vector<std::unique_ptr<Source>> _sources;
	Source *_selected = nullptr;
	QString _selectedId;

};

[[nodiscard]] tgcalls::DesktopCaptureSourceData SourceData() {
	const auto factor = style::DevicePixelRatio();
	const auto size = st::desktopCaptureSourceSize * factor;
	return {
		.aspectSize = { size.width(), size.height() },
		.fps = 1,
		.captureMouse = false,
	};
}

Preview::Preview(tgcalls::DesktopCaptureSource source)
: helper(source, SourceData())
, track(Webrtc::VideoState::Active) {
	helper.setOutput(track.sink());
	helper.start();
}

Source::Source(
	not_null<QWidget*> parent,
	tgcalls::DesktopCaptureSource source,
	const QString &title)
: _widget(parent, st::groupCallRipple)
, _label(&_widget, title, st::desktopCaptureLabel)
, _selectedRect(ImageRoundRadius::Large, st::groupCallMembersBgOver)
, _activeRect(ImageRoundRadius::Large, st::groupCallMuted1)
, _source(source) {
	_widget.paintRequest(
	) | rpl::start_with_next([=] {
		paint();
	}, _widget.lifetime());

	_label.setAttribute(Qt::WA_TransparentForMouseEvents);

	_widget.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto padding = st::desktopCapturePadding;
		_label.resizeToNaturalWidth(
			size.width() - padding.left() - padding.right());
		_label.move(
			(size.width() - _label.width()) / 2,
			size.height() - _label.height() - st::desktopCaptureLabelBottom);
	}, _label.lifetime());

	_widget.setClickedCallback([=] {
		setActive(true);
	});
}

rpl::producer<> Source::activations() const {
	return _activations.events();
}

bool Source::isWindow() const {
	return _source.isWindow();
}

QString Source::deviceIdKey() const {
	return QString::fromStdString(_source.deviceIdKey());
}

void Source::setActive(bool active) {
	if (_active != active) {
		_active = active;
		_widget.update();
		if (active) {
			_activations.fire({});
		}
	}
}

void Source::setGeometry(QRect geometry) {
	_widget.setGeometry(geometry);
}

void Source::clearHelper() {
	_preview = nullptr;
}

void Source::paint() {
	auto p = QPainter(&_widget);

	if (_frame.isNull() && !_preview) {
		setupPreview();
	}
	if (_active) {
		_activeRect.paint(p, _widget.rect());
	} else if (_widget.isOver() || _widget.isDown()) {
		_selectedRect.paint(p, _widget.rect());
	}
	_widget.paintRipple(
		p,
		{ 0, 0 },
		_active ? &st::shadowFg->c : nullptr);

	const auto size = _preview ? _preview->track.frameSize() : QSize();
	const auto factor = style::DevicePixelRatio();
	const auto padding = st::desktopCapturePadding;
	const auto rect = _widget.rect();
	const auto inner = rect.marginsRemoved(padding);
	if (!size.isEmpty()) {
		const auto scaled = size.scaled(inner.size(), Qt::KeepAspectRatio);
		const auto request = Webrtc::FrameRequest{
			.resize = scaled * factor,
			.outer = scaled * factor,
		};
		_frame = _preview->track.frame(request);
		_preview->track.markFrameShown();
	}
	if (!_frame.isNull()) {
		clearHelper();
		const auto size = _frame.size() / factor;
		const auto x = inner.x() + (inner.width() - size.width()) / 2;
		const auto y = inner.y() + (inner.height() - size.height()) / 2;
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(QRect(x, y, size.width(), size.height()), _frame);
	}
}

void Source::setupPreview() {
	_preview = std::make_unique<Preview>(_source);
	_preview->track.renderNextFrame(
	) | rpl::start_with_next([=] {
		if (_preview->track.frameSize().isEmpty()) {
			_preview->track.markFrameShown();
		}
		_widget.update();
	}, _preview->lifetime);
}

rpl::lifetime &Source::lifetime() {
	return _widget.lifetime();
}

ChooseSourceProcess::ChooseSourceProcess(
	not_null<ChooseSourceDelegate*> delegate)
: _delegate(delegate)
, _window(std::make_unique<Ui::Window>())
, _scroll(std::make_unique<ScrollArea>(_window->body()))
, _inner(_scroll->setOwnedWidget(object_ptr<RpWidget>(_scroll.get())))
, _bottom(CreateChild<RpWidget>(_window->body().get()))
, _submit(
	CreateChild<RoundButton>(
		_bottom.get(),
		tr::lng_group_call_screen_share_start(),
		st::desktopCaptureSubmit))
, _finish(
	CreateChild<RoundButton>(
		_bottom.get(),
		tr::lng_group_call_screen_share_stop(),
		st::desktopCaptureFinish))
, _withAudio(
	CreateChild<Checkbox>(
		_bottom.get(),
		tr::lng_group_call_screen_share_audio(tr::now),
		false,
		st::desktopCaptureWithAudio)) {
	setupPanel();
	setupSources();
	activate();
}

void ChooseSourceProcess::Start(not_null<ChooseSourceDelegate*> delegate) {
	auto &map = Map();
	auto i = map.find(delegate);
	if (i == end(map)) {
		i = map.emplace(delegate, nullptr).first;
		delegate->chooseSourceInstanceLifetime().add([=] {
			Map().erase(delegate);
		});
	}
	if (!i->second) {
		i->second = std::make_unique<ChooseSourceProcess>(delegate);
	} else {
		i->second->activate();
	}
}

void ChooseSourceProcess::activate() {
	if (_window->windowState() & Qt::WindowMinimized) {
		_window->showNormal();
	} else {
		_window->show();
	}
	_window->raise();
	_window->activateWindow();
}

[[nodiscard]] base::flat_map<
		not_null<ChooseSourceDelegate*>,
		std::unique_ptr<ChooseSourceProcess>> &ChooseSourceProcess::Map() {
	static auto result = base::flat_map<
		not_null<ChooseSourceDelegate*>,
		std::unique_ptr<ChooseSourceProcess>>();
	return result;
}

void ChooseSourceProcess::setupPanel() {
#ifndef Q_OS_LINUX
	//_window->setAttribute(Qt::WA_OpaquePaintEvent);
#endif // Q_OS_LINUX
	//_window->setAttribute(Qt::WA_NoSystemBackground);

	_window->setWindowIcon(QIcon(
		QPixmap::fromImage(Image::Empty()->original(), Qt::ColorOnly)));
	_window->setTitleStyle(st::desktopCaptureSourceTitle);

	const auto skips = st::desktopCaptureSourceSkips;
	const auto margins = st::desktopCaptureMargins;
	const auto padding = st::desktopCapturePadding;
	const auto bottomSkip = margins.right() + padding.right();
	const auto bottomHeight = 2 * bottomSkip
		+ st::desktopCaptureCancel.height;
	const auto width = margins.left()
		+ kColumns * st::desktopCaptureSourceSize.width()
		+ (kColumns - 1) * skips.width()
		+ margins.right();
	const auto height = margins.top()
		+ kRows * st::desktopCaptureSourceSize.height()
		+ (kRows - 1) * skips.height()
		+ (st::desktopCaptureSourceSize.height() / 2)
		+ bottomHeight;
	_window->setFixedSize({ width, height });
	_window->setStaysOnTop(true);

	_window->body()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_window->body()).fillRect(clip, st::groupCallMembersBg);
	}, _window->lifetime());

	_bottom->setGeometry(0, height - bottomHeight, width, bottomHeight);

	_submit->setClickedCallback([=] {
		if (_selectedId.isEmpty()) {
			return;
		}
		const auto weak = MakeWeak(_window.get());
		_delegate->chooseSourceAccepted(
			_selectedId,
			!_withAudio->isHidden() && _withAudio->checked());
		if (const auto strong = weak.data()) {
			strong->close();
		}
	});
	_finish->setClickedCallback([=] {
		const auto weak = MakeWeak(_window.get());
		_delegate->chooseSourceStop();
		if (const auto strong = weak.data()) {
			strong->close();
		}
	});
	const auto cancel = CreateChild<RoundButton>(
		_bottom.get(),
		tr::lng_cancel(),
		st::desktopCaptureCancel);
	cancel->setClickedCallback([=] {
		_window->close();
	});

	rpl::combine(
		_submit->widthValue(),
		_submit->shownValue(),
		_finish->widthValue(),
		_finish->shownValue(),
		cancel->widthValue()
	) | rpl::start_with_next([=](
			int submitWidth,
			bool submitShown,
			int finishWidth,
			bool finishShown,
			int cancelWidth) {
		_finish->moveToRight(bottomSkip, bottomSkip);
		_submit->moveToRight(bottomSkip, bottomSkip);
		cancel->moveToRight(
			bottomSkip * 2 + (submitShown ? submitWidth : finishWidth),
			bottomSkip);
	}, _bottom->lifetime());

	_withAudio->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto top = (bottomHeight - _withAudio->heightNoMargins()) / 2;
		_withAudio->moveToLeft(bottomSkip, top);
	}, _withAudio->lifetime());

	_withAudio->setChecked(_delegate->chooseSourceActiveWithAudio());
	_withAudio->checkedChanges(
	) | rpl::start_with_next([=] {
		updateButtonsVisibility();
	}, _withAudio->lifetime());

	const auto sharing = !_delegate->chooseSourceActiveDeviceId().isEmpty();
	_finish->setVisible(sharing);
	_submit->setVisible(!sharing);

	_window->body()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_scroll->setGeometry(
			0,
			0,
			size.width(),
			size.height() - _bottom->height());
	}, _scroll->lifetime());

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto rows = int(std::ceil(_sources.size() / float(kColumns)));
		const auto innerHeight = margins.top()
			+ rows * st::desktopCaptureSourceSize.height()
			+ (rows - 1) * skips.height()
			+ margins.bottom();
		_inner->resize(width, std::max(height, innerHeight));
	}, _inner->lifetime());

	if (const auto parent = _delegate->chooseSourceParent()) {
		setupGeometryWithParent(parent);
	}

	_window->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return e->type() == QEvent::Close;
	}) | rpl::start_with_next([=] {
		destroy();
	}, _window->lifetime());
}

void ChooseSourceProcess::setupSources() {
	fillSources();
	setupSourcesGeometry();
}

void ChooseSourceProcess::fillSources() {
	using Type = tgcalls::DesktopCaptureType;
	auto screensManager = tgcalls::DesktopCaptureSourceManager(Type::Screen);
	auto windowsManager = tgcalls::DesktopCaptureSourceManager(Type::Window);

	_withAudio->setVisible(_delegate->chooseSourceWithAudioSupported());

	auto screenIndex = 0;
	auto windowIndex = 0;
	const auto active = _delegate->chooseSourceActiveDeviceId();
	const auto append = [&](const tgcalls::DesktopCaptureSource &source) {
		const auto title = !source.isWindow()
			? tr::lng_group_call_screen_title(
				tr::now,
				lt_index,
				QString::number(++screenIndex))
			: !source.title().empty()
			? QString::fromStdString(source.title())
			: "Window " + QString::number(++windowIndex);
		const auto id = source.deviceIdKey();
		_sources.push_back(std::make_unique<Source>(_inner, source, title));

		const auto raw = _sources.back().get();
		if (!active.isEmpty() && active.toStdString() == id) {
			_selected = raw;
			raw->setActive(true);
		}
		_sources.back()->activations(
		) | rpl::filter([=] {
			return (_selected != raw);
		}) | rpl::start_with_next([=]{
			if (_selected) {
				_selected->setActive(false);
			}
			_selected = raw;
			updateButtonsVisibility();
		}, raw->lifetime());
	};
	for (const auto &source : screensManager.sources()) {
		append(source);
	}
	for (const auto &source : windowsManager.sources()) {
		append(source);
	}
}

void ChooseSourceProcess::updateButtonsVisibility() {
	const auto selectedId = _selected
		? _selected->deviceIdKey()
		: QString();
	if (selectedId == _delegate->chooseSourceActiveDeviceId()
		&& (!_delegate->chooseSourceWithAudioSupported()
			|| (_withAudio->checked()
				== _delegate->chooseSourceActiveWithAudio()))) {
		_selectedId = QString();
		_finish->setVisible(true);
		_submit->setVisible(false);
	} else {
		_selectedId = selectedId;
		_finish->setVisible(false);
		_submit->setVisible(true);
	}
}

void ChooseSourceProcess::setupSourcesGeometry() {
	if (_sources.empty()) {
		destroy();
		return;
	}
	_inner->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto rows = int(std::ceil(_sources.size() / float(kColumns)));
		const auto margins = st::desktopCaptureMargins;
		const auto skips = st::desktopCaptureSourceSkips;
		const auto single = (width
			- margins.left()
			- margins.right()
			- (kColumns - 1) * skips.width()) / kColumns;
		const auto height = st::desktopCaptureSourceSize.height();
		auto top = margins.top();
		auto index = 0;
		for (auto row = 0; row != rows; ++row) {
			auto left = margins.left();
			for (auto column = 0; column != kColumns; ++column) {
				_sources[index]->setGeometry({ left, top, single, height });
				if (++index == _sources.size()) {
					break;
				}
				left += single + skips.width();
			}
			if (index >= _sources.size()) {
				break;
			}
			top += height + skips.height();
		}
	}, _inner->lifetime());

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::start_with_next([=](int scrollTop, int scrollHeight) {
		const auto rows = int(std::ceil(_sources.size() / float(kColumns)));
		const auto margins = st::desktopCaptureMargins;
		const auto skips = st::desktopCaptureSourceSkips;
		const auto height = st::desktopCaptureSourceSize.height();
		auto top = margins.top();
		auto index = 0;
		for (auto row = 0; row != rows; ++row) {
			const auto hidden = (top + height <= scrollTop)
				|| (top >= scrollTop + scrollHeight);
			if (hidden) {
				for (auto column = 0; column != kColumns; ++column) {
					_sources[index]->clearHelper();
					if (++index == _sources.size()) {
						break;
					}
				}
			} else {
				index += kColumns;
			}
			if (index >= _sources.size()) {
				break;
			}
			top += height + skips.height();
		}
	}, _inner->lifetime());
}

void ChooseSourceProcess::setupGeometryWithParent(
		not_null<QWidget*> parent) {
	if (const auto handle = parent->windowHandle()) {
		_window->createWinId();
		const auto parentScreen = handle->screen();
		const auto myScreen = _window->windowHandle()->screen();
		if (parentScreen && myScreen != parentScreen) {
			_window->windowHandle()->setScreen(parentScreen);
		}
	}
	_window->move(
		parent->x() + (parent->width() - _window->width()) / 2,
		parent->y() + (parent->height() - _window->height()) / 2);
}

void ChooseSourceProcess::destroy() {
	auto &map = Map();
	if (const auto i = map.find(_delegate); i != end(map)) {
		if (i->second.get() == this) {
			base::take(i->second);
		}
	}
}

} // namespace

void ChooseSource(not_null<ChooseSourceDelegate*> delegate) {
	ChooseSourceProcess::Start(delegate);
}

} // namespace Calls::Group::Ui::DesktopCapture
