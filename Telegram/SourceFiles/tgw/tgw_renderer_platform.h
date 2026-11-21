/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "core/src/platform_end.h"

#include <deque>

namespace twidget {
class SandboxHostedWait;
} // namespace twidget

namespace Tgw {

class QtBackend;
struct TouchInput;

class HostedTraffic final {
public:
	HostedTraffic(
		std::string bytes,
		std::shared_ptr<const void> hold,
		uint64 number = 0,
		Fn<void(std::string)> respond = nullptr);
	HostedTraffic(HostedTraffic &&other);
	HostedTraffic &operator=(HostedTraffic &&other) = delete;
	~HostedTraffic();

	[[nodiscard]] bool request() const;
	[[nodiscard]] uint64 number() const;
	[[nodiscard]] std::string_view bytes() const;
	void respond(std::string answer);

private:
	std::string _bytes;
	std::shared_ptr<const void> _hold;
	Fn<void(std::string)> _respond;
	uint64 _number = 0;

};

class RendererPlatform final
	: public base::has_weak_ptr
	, private twlite::remote::PlatformEnd::Host
	, private twlite::remote::Channel {
public:
	using Frame = twlite::remote::Frame;

	enum class Down : uchar {
		Lost,
		Sent,
		Live,
	};
	struct Route {
		Fn<void(std::string message)> send;
		Fn<bool(std::string message)> press;
		Fn<void()> released;
		Fn<void(QString reason)> localStop;
	};

	struct Reports {
		Fn<void(Frame &&frame)> frame;
		Fn<void(const twlite::remote::State &state)> state;
		Fn<void(double height)> size;
		Fn<void()> limit;
		Fn<void(const twlite::remote::Pointer &pointer)> pointer;
		Fn<void(QString text)> selection;
		Fn<void(const twlite::remote::ClaimAnswer &answer)> claim;
		Fn<void(std::string_view cursor)> cursor;
		Fn<void(uint64 press)> clicked;
		Fn<void()> content;
		Fn<void()> loaded;
		Fn<void(QString reason)> stopped;
		Fn<void(QString reason)> failed;
		Fn<void(crl::time held)> held;
		Fn<void()> drained;
	};

	RendererPlatform(
		not_null<QtBackend*> backend,
		Route route,
		Reports reports,
		std::shared_ptr<twidget::SandboxHostedWait> wait);
	~RendererPlatform();

	void open(double dpr);
	[[nodiscard]] uint64 requestFrame(bool tick, twlite::remote::Paint paint);
	void setWidth(double width);
	void setMaxHeight(double height);
	[[nodiscard]] bool touch(const TouchInput &input);
	[[nodiscard]] Down press(const TouchInput &input);
	void released();
	[[nodiscard]] bool hover(int action, QPointF point, int modifiers);
	void clearSelection();
	[[nodiscard]] uint64 askClaim(QPointF point);
	void deliverMenus(uint64 press);
	void fingerHold(uint64 press);
	void fingerRelease(uint64 press);
	void refillCanvasFonts();

	void enqueue(HostedTraffic traffic);
	void afterQueued(Fn<void()> done);
	void schedule();
	void close();

	[[nodiscard]] const twlite::remote::State &reportedState() const;
	[[nodiscard]] uint64 lastDown() const;
	[[nodiscard]] bool accepting() const;
	[[nodiscard]] bool waiting() const;
	[[nodiscard]] bool requestWaiting() const;
	[[nodiscard]] bool ended() const;

private:
	struct Received {
		std::optional<Frame> frame;
		std::optional<twlite::remote::State> state;
		std::vector<double> heights;
		std::optional<twlite::remote::Pointer> pointer;
		std::optional<QString> selection;
		std::optional<twlite::remote::ClaimAnswer> claim;
		bool limit = false;
	};

	void frame(Frame &&frame) override;
	void state(const twlite::remote::State &state) override;
	void messages(std::vector<twidget::Json> &&messages) override;
	void pointer(const twlite::remote::Pointer &pointer) override;
	void selection(const twlite::remote::Selection &selection) override;
	void claim(const twlite::remote::ClaimAnswer &answer) override;
	void refused(const std::string &reason) override;
	void send(std::string message) override;

	void pump();
	void consume(HostedTraffic traffic);
	void answer(HostedTraffic &traffic);
	void receive(HostedTraffic &traffic);
	void control(std::string_view message);
	void deliver(Received received);
	void sendControl(std::string message);
	void stopLocally(const QString &reason);
	void fail(QString reason);
	void failEnd();
	void stop(QString reason);
	void end();

	const not_null<QtBackend*> _backend;
	const Route _route;
	const Reports _reports;
	const std::shared_ptr<twidget::SandboxHostedWait> _wait;
	twlite::remote::PlatformEnd _end;
	std::deque<HostedTraffic> _queue;
	Received _received;
	QString _endRefusal;
	Fn<void()> _afterQueued;
	std::size_t _queuedAhead = 0;
	uint64 _downs = 0;
	uint64 _pointers = 0;
	int _requests = 0;
	bool _scheduled = false;
	bool _consuming = false;
	bool _pressing = false;
	bool _pressLive = false;
	bool _content = false;
	bool _loaded = false;
	bool _ended = false;
	bool _closed = false;

};

} // namespace Tgw
