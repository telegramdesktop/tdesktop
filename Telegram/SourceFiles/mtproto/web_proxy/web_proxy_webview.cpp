/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/web_proxy/web_proxy_webview.h"

#include "base/bytes.h"
#include "base/debug_log.h"
#include "base/invoke_queued.h"
#include "config.h"
#include "mtproto/web_proxy/web_proxy_frame.h"
#include "webview/webview_embed.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <crl/crl_time.h>

namespace MTP::WebProxy {
namespace {

constexpr auto kHandshakeTimeout = crl::time(10 * 1000);
constexpr auto kHandshakeTotalTimeout = crl::time(45 * 1000);
constexpr auto kHealthTimeout = crl::time(10 * 1000);
constexpr auto kProbeInterval = crl::time(3 * 1000);
constexpr auto kWriteTimeout = crl::time(10 * 1000);
constexpr auto kMaxPendingBytes = 8 * 1024 * 1024;
constexpr auto kMaxPendingItems = 1024;
constexpr auto kMaxMessageBytes = 2 * 1024 * 1024;

// The relay coalesces DATA up to kMaxFramePayload per frame and the bridge
// posts one frame per native message, so a message can hold a full frame
// in base64 (plus the one-byte prefix); the per-platform script message
// caps in lib_webview must accept at least this much as well.
constexpr auto kMaxFrameMessageBytes = 1
	+ ((kMaxFramePayload + kFrameHeaderSize + 2) / 3) * 4;
static_assert(kMaxMessageBytes >= kMaxFrameMessageBytes);

[[nodiscard]] QString RandomUrlToken(int bytesCount) {
	auto random = bytes::vector(bytesCount);
	bytes::set_random(bytes::make_span(random));
	return QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(random.data()),
		random.size()
	).toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

[[nodiscard]] QString BridgeUrl(
		const ProxyData &proxy,
		const QString &nonce) {
	auto result = QUrl(u"https://"_q + proxy.host);
	result.setPath(u"/"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"bridge"_q, WebProxyBridgeCapability(proxy));
	result.setQuery(query);
	result.setFragment(u"android="_q + nonce);
	return result.toString(QUrl::FullyEncoded);
}

#ifndef NDEBUG
[[nodiscard]] QByteArray RestrictionsProbeScript() {
	return R"JS((()=>{try{
const probe=w=>typeof w.RTCPeerConnection==='undefined'&&typeof w.WebTransport==='undefined'&&typeof w.WebAssembly==='undefined';
let ok=probe(window);
const frame=document.createElement('iframe');
(document.documentElement||document).appendChild(frame);
try{ok=ok&&!!frame.contentWindow&&probe(frame.contentWindow)}finally{frame.remove()}
window.external.invoke('d'+(ok?'1':'0'));
}catch(error){window.external.invoke('d0')}})())JS";
}
#endif // !NDEBUG

[[nodiscard]] QByteArray BridgeScript() {
	return R"JS((()=>{
if(window!==window.top||Object.prototype.hasOwnProperty.call(window,'TelegramWebProxy'))return;
let receiver=null;
const send=value=>window.external.invoke(value);
const encode=value=>{
 const bytes=new Uint8Array(value),parts=[];
 for(let i=0;i<bytes.length;i+=32768)parts.push(String.fromCharCode(...bytes.subarray(i,i+32768)));
 return btoa(parts.join(''));
};
const decode=value=>{
 const binary=atob(value),bytes=new Uint8Array(binary.length);
 for(let i=0;i<binary.length;i++)bytes[i]=binary.charCodeAt(i);
 return bytes.buffer;
};
const bridge={
 postMessage(value){
  if(value instanceof ArrayBuffer)send('b'+encode(value));
  else if(typeof value==='string')send('c'+value);
  else send('f');
 },
 receive(sequence,value){
  try{
   if(typeof receiver!=='function')throw new Error();
   receiver({data:decode(value)});
   send('a'+sequence);
  }catch(error){send('f')}
 },
 receiveControl(sequence,value){
  try{
   if(typeof receiver!=='function')throw new Error();
   receiver({data:value});
   send('a'+sequence);
  }catch(error){send('f')}
 },
 get onmessage(){return receiver},
 set onmessage(value){receiver=typeof value==='function'?value:null}
};
Object.defineProperty(window,'TelegramWebProxy',{
 value:Object.freeze(bridge),configurable:false,writable:false
});
send('h');
})())JS";
}

} // namespace

WebviewCarrier::WebviewCarrier(
		const ProxyData &proxy,
		uint64 generation,
		Callbacks callbacks)
: _proxy(proxy)
, _generation(generation)
, _nonce(RandomUrlToken(32))
, _url(BridgeUrl(proxy, _nonce))
, _callbacks(std::move(callbacks))
, _window(std::make_unique<Webview::Window>(
	nullptr,
	Webview::WindowConfig{
		.storageId = {
			.path = cWorkingDir() + u"tdata/wvproxy"_q,
			.token = QByteArray::fromHex(
				"ec5f15fe14864faaa018d270aa2a0df8"),
		},
		.safe = true,
		.mode = Webview::WindowMode::Hidden,
		.restrictedOrigin = u"https://"_q + proxy.host,
	}))
, _handshakeTimer(std::make_unique<QTimer>())
, _healthTimer(std::make_unique<QTimer>())
, _probeTimer(std::make_unique<QTimer>())
, _writeTimer(std::make_unique<QTimer>()) {
	Expects(QThread::currentThread() == QCoreApplication::instance()->thread());
	Expects(proxy.type == ProxyData::Type::Web);

	if (!_window->valid()) {
		return;
	}
	for (const auto timer : {
			_handshakeTimer.get(),
			_healthTimer.get(),
			_writeTimer.get() }) {
		timer->setSingleShot(true);
	}
	connect(_handshakeTimer.get(), &QTimer::timeout, this, [=] {
		fail("handshake timeout");
	});
	connect(_healthTimer.get(), &QTimer::timeout, this, [=] {
		fail("health timeout");
	});
	connect(_writeTimer.get(), &QTimer::timeout, this, [=] {
		fail("write timeout");
	});
	connect(_probeTimer.get(), &QTimer::timeout, this, [=] {
		_window->eval("window.external?.invoke('h')");
	});
	_window->setMessageHandler([=](Webview::Message message) {
		handleMessage(std::move(message.text), std::move(message.sourceUrl));
	});
	_window->setNavigationStartHandler([=](QString url, bool newWindow) {
		return !newWindow && validNavigation(url);
	});
	_window->setNavigationDoneHandler([=](bool success) {
		if (!success) {
			fail("navigation failed");
		}
	});
	_window->init(BridgeScript());
	_handshakeStarted = crl::now();
	_handshakeTimer->start(kHandshakeTimeout);
	_healthTimer->start(kHealthTimeout);
	_probeTimer->start(kProbeInterval);
	_window->navigate(_url);
}

WebviewCarrier::~WebviewCarrier() {
	close();
}

void WebviewCarrier::close() {
	if (_closing) {
		return;
	}
	_closing = true;
	if (_handshakeTimer) {
		_handshakeTimer->stop();
		_healthTimer->stop();
		_probeTimer->stop();
		_writeTimer->stop();
	}
	_callbacks = Callbacks();
	if (_window && _window->valid()) {
		_window->setMessageHandler(Fn<void(Webview::Message)>());
		_window->setNavigationStartHandler([](QString, bool) {
			return false;
		});
		_window->setNavigationDoneHandler(nullptr);
		if (_adopted && !_failed) {
			_window->eval(
				"window.TelegramWebProxy?.receiveControl(0,'{\"t\":\"close\"}')");
		}
	}
}

bool WebviewCarrier::Supported() {
	return Webview::HiddenSupported();
}

bool WebviewCarrier::valid() const {
	return _window && _window->valid() && !_failed && !_closing;
}

void WebviewCarrier::send(QByteArray frame) {
	enqueue({ std::move(frame), true });
}

void WebviewCarrier::handleMessage(
		std::string message,
		std::string sourceUrl) {
	if (_closing) {
		return;
	} else if (_failed) {
		return;
	} else if (!validSource(sourceUrl)) {
		fail("invalid message source");
		return;
	} else if (message.empty() || message.size() > kMaxMessageBytes) {
		fail("invalid message size");
		return;
	}
	heartbeat();
	const auto data = QByteArray::fromStdString(message);
	switch (data[0]) {
	case 'h':
		if (data.size() != 1) {
			fail("invalid heartbeat");
			return;
		}
		probeRestrictions();
		return;
#ifndef NDEBUG
	case 'd':
		if (data != "d1") {
			LOG(("Web Proxy Error: "
				"Restricted WebView profile probe failed: %1"
				).arg(QString::fromUtf8(data)));
		}
		return;
#endif // !NDEBUG
	case 'f':
		fail("bridge script failure");
		return;
	case 'a': {
		bool ok = false;
		const auto sequence = data.mid(1).toULongLong(&ok);
		if (!ok || _inFlight.frame.isEmpty() || sequence != _writeSequence) {
			fail("invalid write acknowledgement");
			return;
		}
		_writeTimer->stop();
		const auto written = _inFlight.frame.size();
		const auto notify = _inFlight.notifyWritten;
		_pendingBytes -= written;
		_inFlight = Pending();
		if (notify) {
			_callbacks.written(_generation, written);
		}
		drain();
		return;
	} break;
	case 'c':
		handleControl(data.mid(1));
		return;
	case 'b': {
		const auto decoded = QByteArray::fromBase64(
			data.mid(1),
			QByteArray::AbortOnBase64DecodingErrors);
		if (decoded.isEmpty()) {
			fail("invalid binary message");
			return;
		}
		handleBinary(decoded);
		return;
	} break;
	default:
		fail("invalid message type");
	}
}

void WebviewCarrier::handleControl(const QByteArray &control) {
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(control, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		fail("invalid control message");
		return;
	}
	const auto object = document.object();
	const auto type = object.value(u"t"_q).toString();
	if (!_bridgeInitialized && type == u"tproxy-android-init"_q) {
		if (object.value(u"v"_q).toInt() != 1
			|| object.value(u"nonce"_q).toString() != _nonce) {
			fail("invalid bridge initialization");
			return;
		}
		_bridgeInitialized = true;
		enqueue({
			SerializeFrame(FrameType::Hello, 0, QByteArray(1, char(1))),
			false,
		});
	} else if (type == u"close"_q) {
		fail("bridge closed");
	} else if (type == u"status"_q) {
		const auto state = object.value(u"state"_q).toString();
		if (state == u"failed"_q) {
			fail("bridge reported failure");
		} else if (!_adopted
			&& (state == u"connecting"_q || state == u"reconnecting"_q)) {
			extendHandshake();
		}
	}
}

void WebviewCarrier::extendHandshake() {
	const auto left = kHandshakeTotalTimeout
		- (crl::now() - _handshakeStarted);
	if (left <= 0) {
		fail("total handshake timeout");
		return;
	}
	_handshakeTimer->start(int(std::min(kHandshakeTimeout, left)));
}

void WebviewCarrier::probeRestrictions() {
#ifndef NDEBUG
	if (_probed) {
		return;
	}
	_probed = true;
	_window->eval(RestrictionsProbeScript());
#endif // !NDEBUG
}

void WebviewCarrier::handleBinary(QByteArray frame) {
	if (!_bridgeInitialized) {
		fail("binary message before initialization");
		return;
	}
	if (!_adopted) {
		auto input = frame;
		auto frames = std::vector<Frame>();
		if (!ParseFrames(input, frames)
			|| !input.isEmpty()
			|| frames.size() != 1
			|| frames.front().type != FrameType::Welcome
			|| frames.front().streamId != 0
			|| !frames.front().payload.isEmpty()) {
			fail("invalid bridge welcome");
			return;
		}
		_adopted = true;
		_handshakeTimer->stop();
		_callbacks.ready(_generation);
		return;
	}
	_callbacks.payload(_generation, std::move(frame));
}

bool WebviewCarrier::validNavigation(const QString &url) const {
	return QUrl(url) == QUrl(_url);
}

bool WebviewCarrier::validSource(const std::string &sourceUrl) const {
	if (sourceUrl.empty()) {
		return false;
	}
	const auto url = QUrl(QString::fromStdString(sourceUrl));
	const auto expected = QUrl(_url);
	if (!url.isValid()
		|| url.scheme() != u"https"_q
		|| url.host(QUrl::EncodeUnicode) != _proxy.host
		|| url.port(443) != 443
		|| !url.userInfo().isEmpty()
		|| url.path() != u"/"_q
		|| (!url.query().isEmpty()
			&& url.query(QUrl::FullyEncoded)
				!= expected.query(QUrl::FullyEncoded))) {
		return false;
	}
	return url.query().isEmpty()
		? url.fragment().isEmpty()
		: (url.fragment().isEmpty()
			|| url.fragment(QUrl::FullyDecoded) == u"android="_q + _nonce);
}

void WebviewCarrier::enqueue(Pending pending) {
	if (_failed || _closing || pending.frame.isEmpty()) {
		return;
	}
	const auto pendingItems = _pending.size()
		+ (_inFlight.frame.isEmpty() ? 0 : 1);
	if (pendingItems >= kMaxPendingItems
		|| pending.frame.size() > kMaxPendingBytes
		|| _pendingBytes > kMaxPendingBytes - pending.frame.size()) {
		fail("pending write limit exceeded");
		return;
	}
	_pendingBytes += pending.frame.size();
	_pending.push_back(std::move(pending));
	drain();
}

void WebviewCarrier::drain() {
	if (_failed || !_inFlight.frame.isEmpty() || _pending.empty()) {
		return;
	}
	_inFlight = std::move(_pending.front());
	_pending.pop_front();
	++_writeSequence;
	const auto base64 = _inFlight.frame.toBase64();
	_writeTimer->start(kWriteTimeout);
	_window->eval(
		"window.TelegramWebProxy?.receive("
		+ QByteArray::number(_writeSequence)
		+ ",'"
		+ base64
		+ "')");
}

void WebviewCarrier::heartbeat() {
	_healthTimer->start(kHealthTimeout);
}

void WebviewCarrier::fail(const char *reason) {
	if (_failed || _closing) {
		return;
	}
	LOG(("Web Proxy Error: WebView carrier failed: %1"
		).arg(QString::fromLatin1(reason)));
	_failed = true;
	if (_handshakeTimer) {
		_handshakeTimer->stop();
		_healthTimer->stop();
		_probeTimer->stop();
		_writeTimer->stop();
	}
	const auto callback = _callbacks.failed;
	const auto generation = _generation;
	InvokeQueued(QCoreApplication::instance(), [=] {
		callback(generation);
	});
}

} // namespace MTP::WebProxy
