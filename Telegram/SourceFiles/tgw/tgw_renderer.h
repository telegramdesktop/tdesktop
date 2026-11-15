/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/weak_ptr.h"
#include "twidget/gesture_policy.h"
#include "twidget/renderer.h"
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
} // namespace twlite

namespace Tgw {

class HostLink;
class RendererWork;

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

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);
	void hostLinkRequest(const LinkRequest &request);
	void hostBotRequest(const BotRequest &request);
	void hostCopyText(const std::string &text);
	void hostWatch(bool on);

	const std::shared_ptr<HostLink> _link;
	QImage _frame;
	QSize _frameArea;
	rpl::event_stream<Changes> _updates;
	rpl::event_stream<LinkRequest> _linkRequests;
	rpl::event_stream<BotRequest> _botRequests;
	rpl::event_stream<std::string> _copyRequests;
	rpl::event_stream<bool> _watchChanges;
	const std::unique_ptr<RendererWork> _work;

};

} // namespace Tgw
