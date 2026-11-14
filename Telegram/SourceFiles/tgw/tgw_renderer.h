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
#include "twidget/gesture_policy.h"
#include "twidget/renderer.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"

namespace twidget {
class Json;
class WidgetHost;
struct WalletCall;
struct WidgetAction;
struct WidgetRequest;
struct WidgetRequestOutcome;
} // namespace twidget

namespace twlite {
class Renderer;
struct Command;
} // namespace twlite

namespace Tgw {

class CursorStyles;
class HostLink;
class QtBackend;

inline constexpr auto kMinWidth = 200.;
inline constexpr auto kMinHeight = 50.;
inline constexpr auto kMaxHeight = 600.;

[[nodiscard]] int DomModifiers(Qt::KeyboardModifiers modifiers);

class RendererCore final : public base::has_weak_ptr {
public:
	enum class Change : uchar {
		Repaint = (1 << 0),
		Height = (1 << 1),
		Content = (1 << 2),
		Stopped = (1 << 3),
		Loaded = (1 << 4),
	};
	friend inline constexpr bool is_flag_type(Change) { return true; }
	using Changes = base::flags<Change>;
	using Respond = Fn<void(const twidget::WidgetRequestOutcome &outcome)>;
	using RequestHandler = Fn<void(
		const twidget::WidgetRequest &request,
		Respond respond)>;
	using WalletHandler = Fn<void(
		const twidget::WalletCall &call,
		Respond respond)>;
	using CopyHandler = Fn<void(const std::string &text)>;
	using WatchHandler = Fn<void(bool on)>;

	struct LinkRequest {
		std::string url;
		bool telegram = false;
	};
	struct BotRequest {
		std::string data;
		Respond respond;
		std::shared_ptr<const twidget::WalletCall> call;
		bool wallet = false;
	};

	RendererCore(double width, double dpr);
	~RendererCore();

	[[nodiscard]] twidget::RendererFactory hostFactory() const;
	[[nodiscard]] auto hostActionHandler() const
	-> Fn<void(const twidget::WidgetAction &action)>;
	[[nodiscard]] RequestHandler hostRequestHandler() const;
	[[nodiscard]] WalletHandler hostWalletHandler() const;
	[[nodiscard]] CopyHandler hostCopyHandler() const;
	[[nodiscard]] WatchHandler hostWatchHandler() const;
	void bindHost(not_null<twidget::WidgetHost*> host);

	void detach();
	[[nodiscard]] bool attached() const;
	void setOnScreen(bool onScreen);
	[[nodiscard]] bool acceptsInput() const;

	void pointerEnter();
	void pointerMove(QPointF point, int modifiers);
	void pointerLeave();
	void pointerPress(QPointF point, int modifiers);
	void pointerRelease(QPointF point, int modifiers);
	[[nodiscard]] bool menuPress(QPointF point, int modifiers);
	void menuRelease(int modifiers);
	[[nodiscard]] std::optional<twidget::WidgetGestureClaim> fingerPress(
		QPointF point,
		int modifiers);
	void fingerMove(QPointF point, int modifiers);
	void fingerHold();
	void fingerRelease(QPointF point, int modifiers);
	void fingerCancel();
	void resetInput();
	[[nodiscard]] bool hovered() const;
	[[nodiscard]] bool pressed() const;
	[[nodiscard]] bool fingerPressed() const;
	[[nodiscard]] bool handCursorAt(QPointF point);
	[[nodiscard]] bool hasSelection() const;
	[[nodiscard]] QString selectedText() const;
	void clearSelection();

	void setWidth(double width);
	void setMaxHeight(double height);
	[[nodiscard]] double width() const;
	[[nodiscard]] std::optional<double> height() const;
	[[nodiscard]] bool bottomAnchored() const;
	[[nodiscard]] bool hasContent() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] const QString &stopReason() const;
	[[nodiscard]] bool stopFatal() const;
	[[nodiscard]] not_null<twlite::Renderer*> renderer() const;
	[[nodiscard]] const QImage &frame(
		Images::CornersMaskRef corners,
		QSize cardArea,
		bool autoHeight);
	[[nodiscard]] int64 frameBytes() const;
	void dropFrame();
	void refreshFrame();
	void takeFrameFrom(RendererCore &other);

	[[nodiscard]] rpl::producer<Changes> updates() const;
	[[nodiscard]] rpl::producer<LinkRequest> linkRequests() const;
	[[nodiscard]] rpl::producer<BotRequest> botRequests() const;
	[[nodiscard]] rpl::producer<std::string> copyRequests() const;
	[[nodiscard]] rpl::producer<bool> watchChanges() const;

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
		bool moved = false;
		bool active = false;
		bool secondary = false;
	};
	struct Finger {
		QPointF position;
		bool pressed = false;
	};
	struct Held {
		crl::time since = 0;
		int builds = 0;
	};

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);
	void hostLinkRequest(const LinkRequest &request);
	void hostBotRequest(const BotRequest &request);
	void hostCopyText(const std::string &text);
	void hostWatch(bool on);

	[[nodiscard]] int64 elementAt(QPointF point);
	[[nodiscard]] CursorStyles &cursorStyles();
	void holdPointer(QPointF point, int64 hit, bool secondary);
	void trackHover();
	[[nodiscard]] std::vector<twidget::Json> leaveHover();
	void inputDone(const std::vector<twidget::Json> &out, bool wasDirty);
	void releaseInput();
	void stop(QString reason, bool fatal);
	[[nodiscard]] std::vector<twidget::Json> takeSettled();
	void flushQueued();

	[[nodiscard]] bool canTick() const;
	[[nodiscard]] bool clockWanted() const;
	void refreshClock();
	void scheduleWake();
	void dropHeld();
	void releaseHeld();
	[[nodiscard]] bool step(crl::time now);
	void wake();
	void tick();

	[[nodiscard]] std::optional<QString> repaintFrame(
		const std::vector<twlite::Command> &list,
		QRect area,
		Images::CornersMaskRef corners,
		QSize card);

	void finish(const std::vector<twidget::Json> &out, Changes changes);
	[[nodiscard]] Changes forwardAll(const std::vector<twidget::Json> &out);
	[[nodiscard]] Changes applySize(const twidget::Json &message);
	void bindHeightAnchor();
	void forward(const twidget::Json &message);
	void notifyPosted(Changes changes);

	const std::shared_ptr<HostLink> _link;
	std::unique_ptr<QtBackend> _backend;
	std::unique_ptr<twlite::Renderer> _renderer;
	Ui::Animations::Basic _clock;
	base::Timer _wake;
	base::Timer _heldTimer;
	std::optional<Held> _held;
	crl::time _recentReplyUntil = 0;
	QImage _frame;
	QSize _frameSize;
	QSize _frameArea;
	Images::CornersMaskRef _frameCorners;
	std::optional<double> _height;
	std::string _pendingSize;
	QString _stopReason;
	rpl::event_stream<Changes> _updates;
	rpl::event_stream<LinkRequest> _linkRequests;
	rpl::event_stream<BotRequest> _botRequests;
	rpl::event_stream<std::string> _copyRequests;
	rpl::event_stream<bool> _watchChanges;
	std::unique_ptr<CursorStyles> _cursor;
	Pointer _pointer;
	Finger _finger;
	Changes _postedChanges;
	bool _ready = false;
	bool _detached = false;
	bool _hasContent = false;
	bool _acked = false;
	bool _loaded = false;
	bool _heightBottom = false;
	bool _stopped = false;
	bool _stopFatal = false;
	bool _onScreen = false;
	bool _frameRequested = false;
	bool _frameRepaintable = false;
	bool _idleRequest = false;
	bool _stepBusy = false;

};

} // namespace Tgw
