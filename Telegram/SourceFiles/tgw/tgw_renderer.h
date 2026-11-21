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
#include "tgw/tgw_kept_budget.h"
#include "twidget/gesture_policy.h"
#include "ui/image/image_prepare.h"

namespace twidget {
class SandboxHostedChannel;
class SandboxHostedWait;
class WidgetHost;
struct WalletCall;
struct WidgetAction;
struct WidgetRequest;
struct WidgetRequestOutcome;
} // namespace twidget

namespace Tgw {

class HostLink;
class PicturePost;
class RendererWork;
class ReportPost;
struct RendererAnswer;
struct RendererClaim;
struct RendererCursor;
struct RendererLayout;
struct RendererMenu;
struct RendererSelection;

inline constexpr auto kMinWidth = 200.;
inline constexpr auto kMinHeight = 50.;
inline constexpr auto kMaxHeight = 600.;
// widget-sandbox's protocol.md treats a claim not sent in 150 ms as none.
inline constexpr auto kAnswerDeadline = crl::time(150);

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
void DropChannel(std::shared_ptr<twidget::SandboxHostedChannel> channel);

enum class HostedOutcome : uchar {
	None,
	Spared,
	Ordinary,
	Stalled,
	Refused,
};

[[nodiscard]] bool ChargesCrash(HostedOutcome outcome);

struct HostedDecision {
	QString reason;
	HostedOutcome outcome = HostedOutcome::None;
	HostedOutcome transport = HostedOutcome::None;
};

struct HostedWithdrawal {
	HostedOutcome settled = HostedOutcome::None;
	bool ended = false;
	bool awaits = false;
	bool decided = false;
	bool claimed = false;
};

struct HostedRunHooks {
	Fn<void(HostedDecision decision)> decided;
	Fn<void(HostedOutcome transport)> settled;
	Fn<void(HostedWithdrawal withdrawal)> gone;
};

class RendererCore final : public base::has_weak_ptr {
public:
	enum class Change : uchar {
		Repaint = (1 << 0),
		Height = (1 << 1),
		Content = (1 << 2),
		Stopped = (1 << 3),
		Loaded = (1 << 4),
		Client = (1 << 5),
		Cursor = (1 << 6),
		Selection = (1 << 7),
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
	using MenuDecided = Fn<void(bool taken)>;
	using ClaimArrived = Fn<void(twidget::WidgetGestureClaim claim)>;

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
	struct HostedHandlers {
		Fn<void(const std::string &message)> message;
		Fn<void(
			const std::string &message,
			Fn<void(std::string answer)> respond)> request;
		Fn<void(const std::string &reason)> death;
		Fn<bool(
			std::shared_ptr<twidget::SandboxHostedChannel> channel)> channel;
	};

	RendererCore(double width, double dpr);
	~RendererCore();

	[[nodiscard]] uint64 workId() const;
	[[nodiscard]] std::shared_ptr<twidget::SandboxHostedWait> hostedWait() const;
	[[nodiscard]] HostedHandlers hostedHandlers(QString stoppedLabel) const;
	[[nodiscard]] Fn<void(twidget::WidgetHost &host)> keepRoute();
	void releaseRoute(Fn<void()> done = nullptr);
	void setHostedRun(HostedRunHooks hooks);
	[[nodiscard]] HostedWithdrawal withdrawRoute(bool keepOwnDeath = false);
	[[nodiscard]] bool holdRoute(std::shared_ptr<twidget::WidgetHost> host);
	[[nodiscard]] bool deciding() const;
	[[nodiscard]] bool observedDeciding() const;
	void forget();
	[[nodiscard]] bool routeChecked() const;
	[[nodiscard]] bool documentGone() const;
	[[nodiscard]] rpl::producer<> keptUpdates() const;

	[[nodiscard]] auto hostActionHandler() const
	-> Fn<void(const twidget::WidgetAction &action)>;
	[[nodiscard]] RequestHandler hostRequestHandler() const;
	[[nodiscard]] WalletHandler hostWalletHandler() const;
	[[nodiscard]] CopyHandler hostCopyHandler() const;
	[[nodiscard]] WatchHandler hostWatchHandler() const;
	[[nodiscard]] bool bindHost(
		std::weak_ptr<twidget::WidgetHost> host,
		std::weak_ptr<twidget::SandboxHostedChannel> channel);

	void detach();
	[[nodiscard]] bool attached() const;
	void setOnScreen(bool onScreen);
	[[nodiscard]] bool acceptsInput() const;

	void pointerEnter();
	void pointerMove(QPointF point, int modifiers);
	void pointerLeave();
	void pointerPress(QPointF point, int modifiers);
	void pointerRelease(QPointF point, int modifiers);
	[[nodiscard]] bool menuPress(
		QPointF point,
		int modifiers,
		MenuDecided decided);
	void menuRelease(int modifiers);
	[[nodiscard]] bool fingerPress(
		QPointF point,
		int modifiers,
		ClaimArrived claimed);
	void fingerMove(QPointF point, int modifiers);
	void fingerHold();
	void fingerRelease(QPointF point, int modifiers);
	void fingerCancel();
	void resetInput();
	[[nodiscard]] bool pressed() const;
	[[nodiscard]] bool fingerPressed() const;
	[[nodiscard]] bool handCursor() const;
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

	struct Input {
		bool pressed = false;
		bool menuPressed = false;
		bool fingerPressed = false;
		bool hand = false;
	};
	struct PendingMenu {
		MenuDecided decided;
		uint64 serial = 0;
	};
	struct PendingClaim {
		ClaimArrived claimed;
		uint64 serial = 0;
	};

	void hostFrozen();
	void routeLost();
	void hostLinkRequest(const LinkRequest &request);
	void hostBotRequest(const BotRequest &request);
	void hostCopyText(const std::string &text);
	void hostWatch(bool on);
	[[nodiscard]] bool refusesHost() const;
	void work(Fn<void(RendererWork&)> task) const;

	void apply(RendererAnswer &&answer);
	[[nodiscard]] Changes applyLayout(RendererLayout &&layout);
	[[nodiscard]] Changes applyInput(
		std::optional<RendererSelection> &selection,
		const std::optional<RendererCursor> &cursor);
	[[nodiscard]] bool applySelection(RendererSelection &selection);
	void takeReports();
	[[nodiscard]] Fn<void()> takeMenu(const RendererMenu &menu);
	[[nodiscard]] Fn<void()> takeClaim(const RendererClaim &claim);
	void endInput();
	void endMenu();
	void endClaim();
	void forgetSelection();
	void menuDeadline();
	void claimDeadline();

	const std::shared_ptr<HostLink> _link;
	const std::shared_ptr<twidget::SandboxHostedWait> _wait;
	const std::shared_ptr<ReportPost> _reports;
	const std::shared_ptr<PicturePost> _pictures;
	RendererSize _size;
	std::shared_ptr<const KeptBudget::Pixels> _pixels;
	QImage _frame;
	QSize _frameArea;
	std::optional<double> _height;
	QString _stopReason;
	QString _selectedText;
	rpl::event_stream<Changes> _updates;
	rpl::event_stream<LinkRequest> _linkRequests;
	rpl::event_stream<BotRequest> _botRequests;
	rpl::event_stream<std::string> _copyRequests;
	rpl::event_stream<bool> _watchChanges;
	rpl::event_stream<> _keptUpdates;
	std::optional<PendingMenu> _menu;
	std::optional<PendingClaim> _claim;
	base::Timer _menuDeadline;
	base::Timer _claimDeadline;
	uint64 _sizeSerial = 0;
	uint64 _frameSerial = 0;
	uint64 _epoch = 0;
	uint64 _visibility = 0;
	uint64 _pointerSerial = 0;
	uint64 _selectionSerial = 0;
	uint64 _pressSerial = 0;
	Input _input;
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
	bool _hasSelection = false;
	bool _routeChecked = false;
	bool _documentGone = false;

};

} // namespace Tgw
