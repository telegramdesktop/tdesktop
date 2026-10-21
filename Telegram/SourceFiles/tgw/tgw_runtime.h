/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class DocumentData;
class HistoryItem;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace twidget {
class SandboxResources;
class WidgetHost;
} // namespace twidget

namespace Tgw {

class Bridge;
class RendererCore;

struct RuntimeDescriptor {
	not_null<HistoryView::Element*> view;
	not_null<HistoryItem*> item;
	not_null<DocumentData*> document;
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

	[[nodiscard]] not_null<HistoryView::Element*> view() const;
	[[nodiscard]] not_null<HistoryItem*> item() const;
	[[nodiscard]] State state() const;
	[[nodiscard]] bool heavy() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] std::optional<int> height() const;
	[[nodiscard]] RendererCore *core() const;
	[[nodiscard]] RendererCore *interactive() const;

	void setWidth(int width);
	void setBubbleColor(const QColor &color);
	void setWindow(not_null<QWidget*> window);
	void painted();
	void unload();

	[[nodiscard]] QString logText() const;
	void requestLog(Fn<void(const QString &text)> done);

private:
	struct Host;

	static void SubscribeSession(not_null<Main::Session*> session);
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
	[[nodiscard]] static Runtime *FindVictim();

	[[nodiscard]] bool startable() const;
	[[nodiscard]] bool wantsStart() const;
	[[nodiscard]] bool queryVisible() const;

	void start();
	void release();
	void resourcesReady(
		int generation,
		std::shared_ptr<twidget::SandboxResources> resources);
	void construct(std::shared_ptr<twidget::SandboxResources> resources);
	void constructed(
		int generation,
		int source,
		std::shared_ptr<twidget::WidgetHost> host,
		const QString &error);
	void retire(State state);
	void failContent(const QString &error, std::optional<int> source);
	void failResources();

	void subscribeLive();
	void coreUpdated(not_null<RendererCore*> renderer, bool repaint);
	void coreStopped(not_null<RendererCore*> renderer);
	void linkRequested(
		not_null<RendererCore*> renderer,
		const std::string &url,
		bool telegram);

	void syncHostVisibility();
	void syncHostTheme();
	void applyWidth();
	void displayChanged(bool repaint);
	void postShownDisplay();
	void heavyStateChanged(bool wasHeavy);
	void addNote(const QString &text);
	void applySample(uint64 host, uint64 request, bool last, QString text);

	const not_null<HistoryView::Element*> _view;
	const not_null<HistoryItem*> _item;
	const not_null<DocumentData*> _document;
	const Fn<void()> _resize;
	const Fn<void()> _repaint;
	const Fn<void()> _heavyChanged;
	const std::unique_ptr<Bridge> _bridge;
	std::shared_ptr<Host> _host;
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
	std::optional<QRgb> _bubbleColor;
	uint64 _hiddenSerial = 0;
	uint64 _queueSerial = 0;
	uint64 _lastHost = 0;
	uint64 _summaryHost = 0;
	uint64 _summaryRequest = 0;
	uint64 _logHost = 0;
	int _generation = 0;
	int _width = 0;
	State _state = State::Idle;
	bool _visible = false;
	bool _hostVisible = false;
	bool _dark = false;
	bool _hostDark = false;
	bool _painted = false;
	bool _windowKnown = false;
	bool _stopFatal = false;
	bool _widthPosted = false;
	bool _summaryLast = false;
	bool _displayDeferred = false;
	bool _displayPosted = false;

};

[[nodiscard]] Runtime *FindRuntime(
	not_null<const HistoryView::Element*> view);

} // namespace Tgw
