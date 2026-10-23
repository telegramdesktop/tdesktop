/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "twidget/renderer.h"
#include "ui/effects/animations.h"

namespace twidget {
class Json;
struct WidgetAction;
} // namespace twidget

namespace twlite {
class Renderer;
} // namespace twlite

namespace Tgw {

class CursorStyles;
class HostLink;
class QtBackend;

[[nodiscard]] int DomModifiers(Qt::KeyboardModifiers modifiers);

class RendererCore final : public base::has_weak_ptr {
public:
	enum class Change : uchar {
		Repaint = (1 << 0),
		Height = (1 << 1),
		Content = (1 << 2),
		Stopped = (1 << 3),
	};
	friend inline constexpr bool is_flag_type(Change) { return true; }
	using Changes = base::flags<Change>;

	struct LinkRequest {
		std::string url;
		bool telegram = false;
	};

	RendererCore(double width, double dpr);
	~RendererCore();

	[[nodiscard]] twidget::RendererFactory hostFactory() const;
	[[nodiscard]] auto hostActionHandler() const
	-> Fn<void(const twidget::WidgetAction &action)>;

	void detach();
	[[nodiscard]] bool attached() const;
	void setOnScreen(bool onScreen);
	[[nodiscard]] bool acceptsInput() const;

	void pointerEnter();
	void pointerMove(QPointF point, int modifiers);
	void pointerLeave();
	void pointerPress(QPointF point, int modifiers);
	void pointerRelease(QPointF point, int modifiers);
	void resetInput();
	[[nodiscard]] bool hovered() const;
	[[nodiscard]] bool pressed() const;
	[[nodiscard]] bool claimed() const;
	[[nodiscard]] bool handCursorAt(QPointF point);

	void setWidth(double width);
	[[nodiscard]] double width() const;
	[[nodiscard]] std::optional<double> height() const;
	[[nodiscard]] bool hasContent() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] const QString &stopReason() const;
	[[nodiscard]] bool stopFatal() const;
	[[nodiscard]] not_null<twlite::Renderer*> renderer() const;
	[[nodiscard]] const QImage &frame();
	void dropFrame();

	[[nodiscard]] rpl::producer<Changes> updates() const;
	[[nodiscard]] rpl::producer<LinkRequest> linkRequests() const;

private:
	friend class HostLink;

	struct Pointer {
		QPointF position;
		QPointF origin;
		int64 hit = 0;
		int64 hover = 0;
		int64 press = 0;
		int parsed = 0;
		bool over = false;
		bool hovering = false;
		bool pressed = false;
		bool claimed = false;
		bool moved = false;
		bool active = false;
	};

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);
	void hostLinkRequest(const LinkRequest &request);

	[[nodiscard]] int64 elementAt(QPointF point);
	void trackHover();
	[[nodiscard]] std::vector<twidget::Json> leaveHover();
	void inputDone(const std::vector<twidget::Json> &out, bool wasDirty);
	void releaseInput();
	void flushQueued();

	[[nodiscard]] bool canTick() const;
	[[nodiscard]] bool clockWanted() const;
	void refreshClock();
	void scheduleWake();
	[[nodiscard]] bool step();
	void wake();
	void tick();

	void finish(const std::vector<twidget::Json> &out, Changes changes);
	[[nodiscard]] Changes forwardAll(const std::vector<twidget::Json> &out);
	[[nodiscard]] Changes applySize(const twidget::Json &message);
	void forward(const twidget::Json &message);
	void notifyPosted(Changes changes);

	const std::shared_ptr<HostLink> _link;
	std::unique_ptr<QtBackend> _backend;
	std::unique_ptr<twlite::Renderer> _renderer;
	Ui::Animations::Basic _clock;
	base::Timer _wake;
	QImage _frame;
	QSize _frameSize;
	std::optional<double> _height;
	std::string _pendingSize;
	QString _stopReason;
	rpl::event_stream<Changes> _updates;
	rpl::event_stream<LinkRequest> _linkRequests;
	std::unique_ptr<CursorStyles> _cursor;
	Pointer _pointer;
	Changes _postedChanges;
	bool _ready = false;
	bool _detached = false;
	bool _hasContent = false;
	bool _stopped = false;
	bool _stopFatal = false;
	bool _onScreen = false;
	bool _frameRequested = false;
	bool _stepBusy = false;

};

} // namespace Tgw
