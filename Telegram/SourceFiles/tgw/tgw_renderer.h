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

namespace Tgw {

class HostLink;
class RendererWork;
struct RendererAnswer;
struct RendererLayout;

inline constexpr auto kMinWidth = 200.;
inline constexpr auto kMinHeight = 50.;
inline constexpr auto kMaxHeight = 600.;

struct RendererSize {
	double width = 0.;
	double maxHeight = 0.;
	int cardWidth = 0;
	int scale = 0;

	friend inline bool operator==(
		const RendererSize &,
		const RendererSize &) = default;
};

struct RendererCut {
	std::array<QImage, 4> corners;
	int ratio = 0;
	bool autoHeight = false;

	friend inline bool operator==(
		const RendererCut &,
		const RendererCut &) = default;
};

[[nodiscard]] int DomModifiers(Qt::KeyboardModifiers modifiers);
[[nodiscard]] int LogicalHeight(double css, int scale);

class RendererCore final : public base::has_weak_ptr {
public:
	enum class Change : uchar {
		Repaint = (1 << 0),
		Height = (1 << 1),
		Content = (1 << 2),
		Stopped = (1 << 3),
		Loaded = (1 << 4),
		Client = (1 << 5),
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

	void setSize(RendererSize size);
	void setCut(const RendererCut &cut);
	[[nodiscard]] double width() const;
	[[nodiscard]] std::optional<double> height() const;
	[[nodiscard]] bool bottomAnchored() const;
	[[nodiscard]] bool hasContent() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] const QString &stopReason() const;
	[[nodiscard]] bool stopFatal() const;
	[[nodiscard]] QImage frame(QSize cardArea);
	[[nodiscard]] int64 frameBytes() const;
	void dropFrame();
	void refreshFrame();

	[[nodiscard]] rpl::producer<Changes> updates() const;
	[[nodiscard]] rpl::producer<LinkRequest> linkRequests() const;
	[[nodiscard]] rpl::producer<BotRequest> botRequests() const;
	[[nodiscard]] rpl::producer<std::string> copyRequests() const;
	[[nodiscard]] rpl::producer<bool> watchChanges() const;

private:
	friend class HostLink;
	friend class RendererWork;

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);
	void hostLinkRequest(const LinkRequest &request);
	void hostBotRequest(const BotRequest &request);
	void hostCopyText(const std::string &text);
	void hostWatch(bool on);
	[[nodiscard]] bool refusesHost() const;

	void apply(RendererAnswer &&answer);
	[[nodiscard]] Changes applyLayout(RendererLayout &&layout);

	const std::shared_ptr<HostLink> _link;
	RendererSize _size;
	QImage _frame;
	QSize _frameArea;
	std::optional<double> _height;
	QString _stopReason;
	rpl::event_stream<Changes> _updates;
	rpl::event_stream<LinkRequest> _linkRequests;
	rpl::event_stream<BotRequest> _botRequests;
	rpl::event_stream<std::string> _copyRequests;
	rpl::event_stream<bool> _watchChanges;
	uint64 _sizeSerial = 0;
	uint64 _frameSerial = 0;
	uint64 _epoch = 0;
	uint64 _visibility = 0;
	bool _hasContent = false;
	bool _loaded = false;
	bool _heightBottom = false;
	bool _stopped = false;
	bool _stopFatal = false;
	bool _frozen = false;
	bool _detached = false;
	bool _onScreen = false;
	bool _refresh = false;
	bool _undrawn = false;
	const std::unique_ptr<RendererWork> _work;

};

} // namespace Tgw
