/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "tgw/tgw_emoji_images.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_widget_meter.h"

#include <mutex>

namespace twidget {
class SandboxHostedWait;
} // namespace twidget

namespace twlite {
struct Command;
} // namespace twlite

namespace twlite::remote {
struct Frame;
struct State;
} // namespace twlite::remote

namespace Tgw {

class HostedTraffic;
class QtBackend;
class ReportPost;

struct FontPosition {
	QString family;
	QString styleName;
	float64 pixelSize = 0.;
	int weight = 0;
	int style = 0;
	int stretch = 0;
	int hint = 0;
	int strategy = 0;
	int hinting = 0;
	int count = 0;
};

[[nodiscard]] std::vector<FontPosition> CurrentFontPositions();
void AdoptFontPositions(
	const QFont &font,
	const std::vector<FontPosition> &positions);
[[nodiscard]] QString StalledReason();

struct RendererValues {
	QString regularFamily;
	QString monospaceFamily;
	std::vector<FontPosition> positions;
	EmojiSides emoji;
};

struct RendererLayout {
	// WHY: declared first, so it outlives the picture alias it charges.
	std::shared_ptr<const KeptBudget::Pixels> pixels;
	QImage picture;
	QSize area;
	double height = 0.;
	uint64 serial = 0;
	uint64 visibility = 0;
	uint64 epoch = 0;
	bool bottom = false;
	bool client = false;
};

struct RendererState {
	bool hasContent = false;
	bool loaded = false;
};

struct RendererStop {
	QString reason;
	bool fatal = false;
};

struct RendererCursor {
	uint64 serial = 0;
	bool hand = false;
};

struct RendererSelection {
	QString text;
	uint64 serial = 0;
	bool has = false;
};

struct RendererMenu {
	uint64 serial = 0;
	bool taken = false;
};

struct RendererClaim {
	twidget::WidgetGestureClaim claim;
	uint64 serial = 0;
};

struct RendererAnswer {
	RendererState state;
	std::optional<RendererStop> stop;
	std::optional<RendererMenu> menu;
	std::optional<RendererClaim> claim;
	uint64 post = 0;
	bool closed = false;
};

// A hosted work's newest picture for the chat, taken at its answer or later.
class PicturePost final {
public:
	struct Withdrawn {
		bool layout = false;
		bool client = false;
	};

	void offer(RendererLayout layout);
	[[nodiscard]] uint64 send(bool answer);
	[[nodiscard]] Withdrawn withdraw();
	[[nodiscard]] std::optional<RendererLayout> take(
		uint64 post,
		uint64 serial,
		uint64 epoch);
	void setOutdated(bool outdated);
	[[nodiscard]] bool takeOutdated();
	void close();

private:
	std::mutex _mutex;
	std::optional<RendererLayout> _latest;
	uint64 _from = 0;
	uint64 _sent = 0;
	uint64 _answered = 0;
	uint64 _received = 0;
	bool _offered = false;
	bool _outdated = false;
	bool _closed = false;

};

struct RendererGeometry {
	RendererCut cut;
	QSize device;
	QSize area;
	std::optional<double> height;
	uint64 serial = 0;
	uint64 epoch = 0;

	friend inline bool operator==(
		const RendererGeometry &,
		const RendererGeometry &) = default;
};

class RendererReflow final {
public:
	void demand();
	void settle(bool blocked);
	void asked();
	void replied();
	void served(RendererGeometry geometry);
	void follow(const RendererGeometry &geometry);
	[[nodiscard]] std::optional<bool> outdated(
		const RendererGeometry &geometry);

	[[nodiscard]] bool owed() const;
	[[nodiscard]] bool shows(const RendererGeometry &geometry) const;

private:
	enum class Owed : uchar {
		None,
		Correction,
		Demand,
	};

	std::optional<RendererGeometry> _served;
	std::optional<RendererGeometry> _told;
	Owed _owed = Owed::None;
	bool _correction = false;

};

struct RendererHosted {
	std::shared_ptr<twidget::SandboxHostedWait> wait;
	std::shared_ptr<ReportPost> reports;
	std::shared_ptr<PicturePost> pictures;
};

class RendererWork final : public base::has_weak_ptr {
public:
	using Change = RendererCore::Change;
	using Changes = RendererCore::Changes;

	struct Host {
		Fn<void(const std::string &kind, const std::string &detail)> diag;
		Fn<std::optional<bool>()> bottom;
		Fn<bool()> gone;
		Fn<void(std::string message)> platform;
		Fn<bool(std::string message)> press;
		Fn<void()> released;
		Fn<bool(QString reason)> localStop;
	};

	RendererWork(
		uint64 id,
		base::weak_ptr<RendererCore> chat,
		double dpr,
		const RendererValues &values,
		Host host,
		RendererHosted hosted);
	~RendererWork();

	static void Create(
		uint64 id,
		base::weak_ptr<RendererCore> chat,
		double dpr,
		const RendererValues &values,
		Host host,
		RendererHosted hosted);
	[[nodiscard]] static RendererWork *Find(uint64 id);
	static void Destroy(uint64 id);
	static void DestroyAll();
	[[nodiscard]] static double DeviceRatio(double dpr);

	[[nodiscard]] WidgetMeter::Operation operation();

	void freeze(QString reason, bool fatal);
	void hostDied(Fn<void()> reached);

	void detach();
	void setOnScreen(bool onScreen, uint64 visibility);

	void pointerEnter(uint64 serial);
	void pointerMove(QPointF point, int modifiers, uint64 serial);
	void pointerLeave(uint64 serial);
	void pointerPress(
		QPointF point,
		int modifiers,
		uint64 serial,
		uint64 selection);
	void pointerRelease(
		QPointF point,
		int modifiers,
		bool cancel,
		uint64 serial);
	void menuPress(QPointF point, int modifiers, uint64 serial);
	void menuDecided(uint64 serial, bool deliver);
	void menuRelease(int modifiers, bool cancel);
	void fingerPress(QPointF point, int modifiers, uint64 serial);
	void fingerMove(QPointF point, int modifiers);
	void fingerHold();
	void fingerRelease(QPointF point, int modifiers);
	void fingerCancel();
	void resetInput();
	void clearSelection(uint64 serial);

	void setSize(const RendererSize &size, uint64 serial);
	void setCut(const RendererCut &cut);
	void demand();
	void drawn();
	void dropFrame(uint64 epoch);
	void refreshFrame();

	void platformTraffic(HostedTraffic traffic);
	void routeEnded();

private:
	struct Hosted;
	struct ListPaint {
		KeptBudget::Backing backing;
		std::optional<RendererLayout> layout;
	};

	static void EmojiTilesLanded(std::optional<int> glyph);

	[[nodiscard]] std::unique_ptr<Hosted> makeHosted(RendererHosted hosted);
	[[nodiscard]] bool dirty() const;
	[[nodiscard]] bool wantsTick() const;
	[[nodiscard]] double nextTickIn() const;
	[[nodiscard]] int viewportHeight() const;
	[[nodiscard]] bool routeAlive() const;

	[[nodiscard]] bool acceptsInput() const;
	void reportInput(RendererAnswer &answer);
	void cancelInput();
	void releaseInput();
	void finishDetach();

	void stop(QString reason, bool fatal);
	void refuse(QString limit);
	void backendRefused();

	[[nodiscard]] bool canTick() const;
	[[nodiscard]] bool clockWanted() const;
	void refreshClock();
	void startClock();
	void stopClock();
	void clockStep();
	void scheduleWake();
	[[nodiscard]] bool step();
	void wake();
	void tick();

	void schedule();
	void work();
	void applySizeRequest();
	[[nodiscard]] bool rasterWanted() const;
	[[nodiscard]] RendererGeometry geometry() const;
	[[nodiscard]] bool geometryPainted() const;
	[[nodiscard]] QSize layoutArea() const;
	[[nodiscard]] std::optional<QSize> frameSize() const;
	[[nodiscard]] bool framed(const RendererCut &cut, QSize cardArea) const;
	void raster();
	[[nodiscard]] ListPaint paintList(
		const std::vector<twlite::Command> &list,
		QSize size);
	[[nodiscard]] std::optional<RendererLayout> completed(QSize cardArea);
	void releaseCanvas();
	void dropCanvas();
	void deliver(RendererAnswer &&answer);

	[[nodiscard]] bool inboundFirst() const;
	[[nodiscard]] bool sealedAtStop() const;
	[[nodiscard]] bool awaitingFrame() const;
	void requestPaint();
	void hostedTick();
	void askTick();
	void hostedFrame(twlite::remote::Frame &&frame);
	void paintFrame(
		const std::vector<twlite::Command> &list,
		QSize device);
	void hostedState(const twlite::remote::State &state);
	[[nodiscard]] bool takeState(
		const twlite::remote::State &state,
		bool current);
	void hostedSize(double height);
	void hostedContent();
	void hostedLoaded();
	void hostedHeld();
	void unblock();
	void resumeInbound();
	void closeRoute(bool checked);
	void refuseRoute(QString limit, const char *detail = nullptr);

	[[nodiscard]] Changes applyHeight(double height);
	void bindHeightAnchor();
	void post(Changes changes);

	const uint64 _id = 0;
	const base::weak_ptr<RendererCore> _chat;
	const Host _host;
	const double _dpr = 0.;
	std::unique_ptr<QtBackend> _backend;
	std::unique_ptr<Hosted> _hosted;
	base::Timer _clock;
	base::Timer _wake;
	RendererSize _size;
	std::optional<RendererCut> _cut;
	QImage _frame;
	RendererGeometry _painted;
	RendererReflow _reflow;
	std::optional<double> _height;
	QString _stopReason;
	EmojiWaits _emojiWaits;
	EmojiWaits _emojiPainted;
	Changes _postedChanges;
	uint64 _sizeSerial = 0;
	uint64 _epoch = 0;
	uint64 _visibility = 0;
	bool _detached = false;
	bool _detaching = false;
	bool _hasContent = false;
	bool _loaded = false;
	bool _heightBottom = false;
	bool _stopped = false;
	bool _stopFatal = false;
	bool _onScreen = false;
	bool _stepBusy = false;
	bool _clockOn = false;
	bool _scheduled = false;
	bool _sizePending = false;
	bool _rasterThrottled = false;
	bool _undrawn = false;
	bool _client = false;
	bool _reported = false;

};

} // namespace Tgw
