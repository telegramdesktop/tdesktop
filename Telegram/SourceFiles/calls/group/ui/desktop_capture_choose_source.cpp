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
#include "base/platform/base_platform_info.h"
#include "webrtc/webrtc_video_track.h"
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

class Source final {
public:
	Source(
		not_null<QWidget*> parent,
		tgcalls::DesktopCaptureSource source,
		const QString &title);

	void setGeometry(QRect geometry);
	void clearHelper();

	[[nodiscard]] bool ready() const;
	[[nodiscard]] rpl::producer<> clicks() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void paint();
	void setupPreview();

	AbstractButton _widget;
	FlatLabel _label;
	tgcalls::DesktopCaptureSource _source;
	std::unique_ptr<Preview> _preview;
	QImage _frame;

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
	void destroy();

	static base::flat_map<
		not_null<ChooseSourceDelegate*>,
		std::unique_ptr<ChooseSourceProcess>> &Map();

	const not_null<ChooseSourceDelegate*> _delegate;
	const std::unique_ptr<::Ui::Window> _window;
	const std::unique_ptr<ScrollArea> _scroll;
	const not_null<RpWidget*> _inner;

	std::vector<std::unique_ptr<Source>> _sources;

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
: _widget(parent)
, _label(&_widget, title)
, _source(source) {
	_widget.paintRequest(
	) | rpl::start_with_next([=] {
		paint();
	}, _widget.lifetime());

	_widget.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_label.resizeToNaturalWidth(size.width());
		_label.move(
			(size.width() - _label.width()) / 2,
			size.height() - _label.height());
	}, _label.lifetime());
}

rpl::producer<> Source::clicks() const {
	return _widget.clicks() | rpl::to_empty;
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
	const auto size = _preview ? _preview->track.frameSize() : QSize();
	const auto factor = style::DevicePixelRatio();
	const auto rect = _widget.rect();
	const auto inner = QRect(
		rect.x(),
		rect.y(),
		rect.width(),
		rect.height() - _label.height());
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
		} else {
			_widget.update();
		}
	}, _preview->lifetime);
}

rpl::lifetime &Source::lifetime() {
	return _widget.lifetime();
}

ChooseSourceProcess::ChooseSourceProcess(
	not_null<ChooseSourceDelegate*> delegate)
: _delegate(delegate)
, _window(std::make_unique<::Ui::Window>())
, _scroll(std::make_unique<ScrollArea>(_window->body()))
, _inner(_scroll->setOwnedWidget(object_ptr<RpWidget>(_scroll.get()))) {
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
	const auto width = kColumns * st::desktopCaptureSourceSize.width()
		+ (kColumns + 1) * st::desktopCaptureSourceSkip;
	const auto height = kRows * st::desktopCaptureSourceSize.height()
		+ (kRows + 1) * st::desktopCaptureSourceSkip
		+ (st::desktopCaptureSourceSize.height() / 2);
	_window->setFixedSize({ width, height });
	_window->setWindowFlags(Qt::WindowStaysOnTopHint);

	_window->body()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_scroll->setGeometry({ QPoint(), size });
	}, _scroll->lifetime());

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto rows = int(std::ceil(_sources.size() / float(kColumns)));
		const auto height = rows * st::desktopCaptureSourceSize.height()
			+ (rows + 1) * st::desktopCaptureSourceSkip;
		_inner->resize(width, height);
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

	auto screenIndex = 0;
	auto windowIndex = 0;
	const auto append = [&](const tgcalls::DesktopCaptureSource &source) {
		const auto title = !source.title().empty()
			? QString::fromStdString(source.title())
			: source.isWindow()
			? "Window " + QString::number(++windowIndex)
			: "Screen " + QString::number(++screenIndex);
		_sources.push_back(std::make_unique<Source>(_inner, source, title));
		_sources.back()->clicks(
		) | rpl::start_with_next([=, id = source.deviceIdKey()]{
			_delegate->chooseSourceAccepted(QString::fromStdString(id));
			}, _sources.back()->lifetime());
	};
	for (const auto &source : screensManager.sources()) {
		append(source);
	}
	for (const auto &source : windowsManager.sources()) {
		append(source);
	}
}

void ChooseSourceProcess::setupSourcesGeometry() {
	if (_sources.empty()) {
		//LOG(());
		destroy();
		return;
	}
	_inner->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto rows = int(std::ceil(_sources.size() / float(kColumns)));
		const auto skip = st::desktopCaptureSourceSkip;
		const auto single = (width - (kColumns + 1) * skip) / kColumns;
		const auto height = st::desktopCaptureSourceSize.height();
		auto top = skip;
		auto index = 0;
		for (auto row = 0; row != rows; ++row) {
			auto left = skip;
			for (auto column = 0; column != kColumns; ++column) {
				_sources[index]->setGeometry({ left, top, single, height });
				if (++index == _sources.size()) {
					break;
				}
				left += single + skip;
			}
			if (index >= _sources.size()) {
				break;
			}
			top += height + skip;
		}
	}, _inner->lifetime());

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::start_with_next([=](int scrollTop, int scrollHeight) {
		const auto rows = int(std::ceil(_sources.size() / float(kColumns)));
		const auto skip = st::desktopCaptureSourceSkip;
		const auto height = st::desktopCaptureSourceSize.height();
		auto top = skip;
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
			top += height + skip;
		}
	}, _inner->lifetime());
}

void ChooseSourceProcess::setupGeometryWithParent(
		not_null<QWidget*> parent) {
	if (const auto handle = parent->windowHandle()) {
		if (::Platform::IsLinux()) {
			_window->windowHandle()->setTransientParent(
				parent->windowHandle());
			_window->setWindowModality(Qt::WindowModal);
		}
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
