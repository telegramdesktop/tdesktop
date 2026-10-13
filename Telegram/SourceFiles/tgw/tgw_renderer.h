/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/weak_ptr.h"
#include "twidget/renderer.h"

namespace twidget {
class Json;
} // namespace twidget

namespace twlite {
class Backend;
class Renderer;
} // namespace twlite

namespace Tgw {

class HostLink;

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

	struct BridgeCall {
		std::string method;
		std::string arguments;
	};

	RendererCore(double width, double dpr);
	~RendererCore();

	[[nodiscard]] twidget::RendererFactory hostFactory() const;
	[[nodiscard]] auto hostBridgeHandler() const
	-> Fn<void(const std::string &method, const std::string &arguments)>;

	void detach();
	[[nodiscard]] bool attached() const;

	void setWidth(double width);
	[[nodiscard]] double width() const;
	[[nodiscard]] std::optional<double> height() const;
	[[nodiscard]] bool hasContent() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] const QString &stopReason() const;
	[[nodiscard]] bool stopFatal() const;
	[[nodiscard]] not_null<twlite::Renderer*> renderer() const;

	[[nodiscard]] rpl::producer<Changes> updates() const;
	[[nodiscard]] rpl::producer<BridgeCall> bridgeCalls() const;

private:
	friend class HostLink;

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);
	void hostBridgeCall(const BridgeCall &call);

	void finish(const std::vector<twidget::Json> &out, Changes changes);
	[[nodiscard]] Changes applySize(const twidget::Json &message);
	void forward(const twidget::Json &message);

	const std::shared_ptr<HostLink> _link;
	std::unique_ptr<twlite::Backend> _backend;
	std::unique_ptr<twlite::Renderer> _renderer;
	std::optional<double> _height;
	std::string _pendingSize;
	QString _stopReason;
	rpl::event_stream<Changes> _updates;
	rpl::event_stream<BridgeCall> _bridgeCalls;
	bool _ready = false;
	bool _detached = false;
	bool _hasContent = false;
	bool _stopped = false;
	bool _stopFatal = false;

};

} // namespace Tgw
