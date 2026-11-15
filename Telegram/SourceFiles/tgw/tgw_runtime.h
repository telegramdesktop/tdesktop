/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "tgw/tgw_media.h"

class HistoryItem;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace twidget {
class SandboxResources;
struct WalletCall;
struct WidgetData;
class WidgetHost;
struct WidgetRequestOutcome;
struct WidgetTheme;
} // namespace twidget

namespace Tgw {

class Bridge;
class RendererCore;
struct MessageMemory;

enum class HeightChange : uchar {
	Own,
	Client,
};

enum class ResizeAnchor : uchar {
	Top,
	Bottom,
};

struct HeightLayout {
	int serial = 0;
	int height = 0;
	HeightChange change = HeightChange::Client;
	ResizeAnchor anchor = ResizeAnchor::Top;
};

struct ContentSafeArea {
	int bottom = 0;
	int cornerLeft = 0;
	int cornerRight = 0;

	friend inline bool operator==(ContentSafeArea, ContentSafeArea) = default;
};

struct RuntimeDescriptor {
	not_null<HistoryView::Element*> view;
	not_null<HistoryItem*> item;
	QByteArray html;
	MessageContext context;
	Fn<void()> resize;
	Fn<void()> repaint;
	Fn<void()> heavyChanged;
};

class Runtime final : public base::has_weak_ptr {
public:
	enum class State : uchar {
		Idle,
		Waiting,
		Constructing,
		Live,
		Released,
		Stopped,
		Failed,
	};

	explicit Runtime(RuntimeDescriptor &&descriptor);
	~Runtime();

	[[nodiscard]] static bool IsQuitPrevent();
	[[nodiscard]] static bool IsExitPrevent(Fn<void()> retry);
	static void StateChanged(
		const base::flat_set<not_null<HistoryItem*>> &items);
	[[nodiscard]] static auto WatchingOnScreen()
	-> std::vector<not_null<HistoryItem*>>;

	[[nodiscard]] not_null<HistoryView::Element*> view() const;
	[[nodiscard]] not_null<HistoryItem*> item() const;
	[[nodiscard]] bool matches(
		const QByteArray &html,
		const MessageContext &context) const;
	[[nodiscard]] State state() const;
	[[nodiscard]] bool heavy() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] std::optional<int> height() const;
	[[nodiscard]] RendererCore *core() const;
	[[nodiscard]] RendererCore *interactive() const;
	[[nodiscard]] std::optional<HeightLayout> heightLayout() const;

	void attachView(
		not_null<HistoryView::Element*> view,
		Fn<void()> resize,
		Fn<void()> repaint,
		Fn<void()> heavyChanged);
	void setSize(int width, double maxHeight);
	void setSafeArea(ContentSafeArea area);
	void laidOut(int height);
	void setTheme(std::shared_ptr<const twidget::WidgetTheme> theme);
	void setWindow(not_null<QWidget*> window);
	void painted();
	void unload();

	[[nodiscard]] QString logText() const;
	void requestLog(Fn<void(const QString &text)> done);

private:
	struct Host;

	[[nodiscard]] static std::shared_ptr<MessageMemory> RememberMessage(
		not_null<HistoryItem*> item,
		const QByteArray &html);
	static void PostCheck();
	static void Check();
	static void RefreshVisibility();
	static void ScheduleStarts();
	static void Drain();
	static void Sample(const std::shared_ptr<Host> &host);
	static void SampleDone(
		const std::shared_ptr<Host> &host,
		uint64 request,
		bool last,
		QString text);
	static void ThemeDone(const std::shared_ptr<Host> &host);
	static void WalletAvailabilityChanged(not_null<Main::Session*> session);
	[[nodiscard]] static Runtime *FindVictim();
	static void TrimKeptPictures();

	[[nodiscard]] bool currentContent() const;
	[[nodiscard]] bool crashedOut() const;
	[[nodiscard]] bool startable() const;
	[[nodiscard]] bool wantsStart() const;
	[[nodiscard]] bool queryVisible() const;

	[[nodiscard]] bool recordStop(not_null<RendererCore*> renderer);
	void breakCrashRun();
	void start();
	void release();
	void resourcesReady(
		int generation,
		std::shared_ptr<twidget::SandboxResources> resources);
	void construct(std::shared_ptr<twidget::SandboxResources> resources);
	void constructed(
		int generation,
		std::shared_ptr<twidget::WidgetHost> host,
		const twidget::WidgetData &data,
		const QString &error,
		uint64 storage);
	void retire(State state);
	void stopForCrashes();
	void fail(const QString &error);
	void failResources();

	void subscribeLive();
	void coreUpdated(
		not_null<RendererCore*> renderer,
		bool repaint,
		bool finished);
	void coreStopped(not_null<RendererCore*> renderer);
	void linkRequested(
		not_null<RendererCore*> renderer,
		const std::string &url,
		bool telegram);
	void botRequested(
		not_null<RendererCore*> renderer,
		const std::string &data,
		bool wallet,
		const twidget::WalletCall *call,
		Fn<void(const twidget::WidgetRequestOutcome &outcome)> respond);
	void walletCalled(
		const twidget::WalletCall &call,
		Fn<void(const twidget::WidgetRequestOutcome &outcome)> respond);
	void copyRequested(
		not_null<RendererCore*> renderer,
		const std::string &text);
	void watchChanged(not_null<RendererCore*> renderer, bool on);

	void syncHostVisibility();
	void syncHostTheme();
	void syncHostData();
	void syncHostSafeArea();
	void applySize();
	[[nodiscard]] int effectiveHeight() const;
	[[nodiscard]] ResizeAnchor heightAnchor() const;
	void markHeight(bool client);
	void displayChanged(bool repaint);
	void postShownDisplay();
	void emojiRefreshed();
	void heavyStateChanged(bool wasHeavy);
	void addNote(const QString &text);
	void applySample(uint64 host, uint64 request, bool last, QString text);

	not_null<HistoryView::Element*> _view;
	const not_null<HistoryItem*> _item;
	const QByteArray _html;
	const MessageContext _context;
	Fn<void()> _resize;
	Fn<void()> _repaint;
	Fn<void()> _heavyChanged;
	const std::unique_ptr<Bridge> _bridge;
	const std::shared_ptr<MessageMemory> _memory;
	std::shared_ptr<Host> _host;
	std::shared_ptr<const twidget::WidgetTheme> _theme;
	std::shared_ptr<const twidget::WidgetTheme> _hostTheme;
	std::unique_ptr<RendererCore> _live;
	std::unique_ptr<RendererCore> _still;
	std::vector<QString> _notes;
	QString _stopReason;
	QString _summary;
	Fn<void(const QString &text)> _logDone;
	QPointer<QWidget> _window;
	rpl::lifetime _liveLifetime;
	rpl::lifetime _windowLifetime;
	std::optional<int> _shownHeight;
	const uint64 _contentSerial = 0;
	uint64 _hiddenSerial = 0;
	uint64 _keptSerial = 0;
	uint64 _queueSerial = 0;
	uint64 _lastHost = 0;
	uint64 _summaryHost = 0;
	uint64 _summaryRequest = 0;
	uint64 _logHost = 0;
	double _maxHeight = 0.;
	int _generation = 0;
	int _width = 0;
	HeightLayout _layout;
	ContentSafeArea _safeArea;
	ContentSafeArea _hostSafeArea;
	State _state = State::Idle;
	bool _visible = false;
	bool _hostVisible = false;
	bool _painted = false;
	bool _windowKnown = false;
	bool _stopFatal = false;
	bool _summaryLast = false;
	bool _displayDeferred = false;
	bool _displayPosted = false;
	bool _sizing = false;
	bool _clientPending = false;

};

[[nodiscard]] double CssFromLogical(int logical);
[[nodiscard]] int LogicalHeight(double css);
[[nodiscard]] int LogicalWidth(double css);

[[nodiscard]] Runtime *FindRuntime(
	not_null<const HistoryView::Element*> view);

} // namespace Tgw
