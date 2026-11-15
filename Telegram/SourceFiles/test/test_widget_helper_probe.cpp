/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_probe.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "platform/platform_widget_helper.h"
#include "settings.h"

#include <QtCore/QStandardPaths>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <delayimp.h>
#include <winternl.h>

#include <QtCore/QDir>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>
#endif // Q_OS_WIN

namespace Test {
namespace {

[[nodiscard]] QString ProbeMode() {
	return QString::fromLatin1(kWidgetHelperProbeMode);
}

[[nodiscard]] QString UnsandboxedMode() {
	return QString::fromLatin1(kWidgetHelperUnsandboxedMode);
}

[[nodiscard]] QString ConnectControlArgument() {
	return QString::fromLatin1(kWidgetHelperConnectControl);
}

#ifdef Q_OS_WIN

// At most this many network targets, the loopback port's four included:
// a TCP one can wait kConnectWaitMs in each phase, all before Ready.
constexpr auto kTargetLimit = 16;
constexpr auto kTargetTextLimit = 64;
constexpr auto kPortTextLimit = 16;
constexpr auto kConnectWaitMs = 1000;
constexpr auto kBeforeLowering = "before lowering";
constexpr auto kAfterLowering = "after lowering";
constexpr auto kStatusAccessDenied = ULONG(0xC0000022);
constexpr auto kAfdEndpoint = L"\\Device\\Afd\\Endpoint";

// Winsock's own endpoint-open attribute, as mswsock.dll passes it to
// NtCreateFile for socket(): ReactOS's AFD_CREATE_PACKET (endpoint flags,
// group, transport name) with the family, type and protocol that
// mswsock.dll writes between the group and the empty transport name. UDP
// endpoints are AFD_ENDPOINT_CONNECTIONLESS | AFD_ENDPOINT_MESSAGE_ORIENTED.
constexpr auto kAfdEaName = std::string_view("AfdOpenPacketXX");
constexpr auto kAfdConnectionless = ULONG(0x1);
constexpr auto kAfdMessageOriented = ULONG(0x10);

// FILE_FULL_EA_INFORMATION of the DDK's ntifs.h.
struct FullEaInformation {
	ULONG nextEntryOffset = 0;
	UCHAR flags = 0;
	UCHAR nameLength = 0;
	USHORT valueLength = 0;
	CHAR name[1] = {};
};

struct AfdOpenPacket {
	ULONG endpointFlags = 0;
	LONG group = 0;
	ULONG family = 0;
	ULONG type = 0;
	ULONG protocol = 0;
	ULONG transportNameLength = 0;
	WCHAR transportName[1] = {};
};

struct AfdEa {
	std::vector<ULONG> storage;
	ULONG size = 0;
	ULONG endpointFlags = 0;
};

struct SocketKind {
	int family = 0;
	int type = 0;
	int protocol = 0;
	const char *name = nullptr;
};

constexpr auto kSocketKinds = std::array{
	SocketKind{
		AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		"AF_INET, SOCK_STREAM, IPPROTO_TCP",
	},
	SocketKind{
		AF_INET,
		SOCK_DGRAM,
		IPPROTO_UDP,
		"AF_INET, SOCK_DGRAM, IPPROTO_UDP",
	},
	SocketKind{
		AF_INET6,
		SOCK_STREAM,
		IPPROTO_TCP,
		"AF_INET6, SOCK_STREAM, IPPROTO_TCP",
	},
	SocketKind{
		AF_INET6,
		SOCK_DGRAM,
		IPPROTO_UDP,
		"AF_INET6, SOCK_DGRAM, IPPROTO_UDP",
	},
};

struct NtFunctions {
	decltype(&::NtCreateFile) create = nullptr;
	decltype(&::RtlInitUnicodeString) init = nullptr;
};

class Socket final {
public:
	Socket() = default;
	explicit Socket(SOCKET handle);
	Socket(Socket &&other) noexcept;
	Socket &operator=(Socket &&other) noexcept;
	~Socket();

	[[nodiscard]] SOCKET get() const;
	[[nodiscard]] explicit operator bool() const;
	[[nodiscard]] int close();

private:
	SOCKET _handle = INVALID_SOCKET;

};

struct Target {
	QString text;
	SOCKADDR_STORAGE address = {};
	int length = 0;
	bool tcp = false;
	Socket preopened;
};

struct Endpoint {
	const SocketKind *kind = nullptr;
	AfdEa ea;
};

// The harness's own network controls and attempts around the lowering,
// parsed from the argv tail; its destructor closes every socket it still
// holds and ends its Winsock start.
class NetworkProbe final {
public:
	explicit NetworkProbe(const QStringList &arguments);
	NetworkProbe(const NetworkProbe &) = delete;
	NetworkProbe &operator=(const NetworkProbe &) = delete;
	~NetworkProbe();

	void prepare();
	void attempt();
	[[nodiscard]] const QStringList &lines() const;

private:
	void parse(const QStringList &arguments);
	void addTarget(const QString &text);
	void malformed(const QString &what);
	void startWinsock();
	void resolveTargets();
	void resolveNative();
	void attemptKinds(const QString &phase);
	void attemptTargets(const QString &phase, const char *markerPhase);
	void attemptEndpoints(const QString &phase, bool control);
	void preopen();
	void attemptPreopened();
	void close();

	QString _mode;
	QStringList _lines;
	QStringList _targetTexts;
	std::vector<Target> _targets;
	std::vector<Endpoint> _endpoints;
	NtFunctions _nt;
	UNICODE_STRING _afdName = {};
	int _loopbackPort = 0;
	bool _connectControl = false;
	bool _winsock = false;

};

Socket::Socket(SOCKET handle)
: _handle(handle) {
}

Socket::Socket(Socket &&other) noexcept
: _handle(std::exchange(other._handle, INVALID_SOCKET)) {
}

Socket &Socket::operator=(Socket &&other) noexcept {
	if (this != &other) {
		if (_handle != INVALID_SOCKET) {
			::closesocket(_handle);
		}
		_handle = std::exchange(other._handle, INVALID_SOCKET);
	}
	return *this;
}

Socket::~Socket() {
	if (_handle != INVALID_SOCKET) {
		::closesocket(_handle);
	}
}

SOCKET Socket::get() const {
	return _handle;
}

Socket::operator bool() const {
	return (_handle != INVALID_SOCKET);
}

int Socket::close() {
	const auto handle = std::exchange(_handle, INVALID_SOCKET);
	return (handle == INVALID_SOCKET || !::closesocket(handle))
		? 0
		: WSAGetLastError();
}

// The client's working directory argument as a native path, checked for
// emptiness before cleaning: an empty one stays empty and names no target.
[[nodiscard]] QString NativeDirectory(const QString &path) {
	return path.isEmpty()
		? QString()
		: QDir::toNativeSeparators(QDir::cleanPath(path));
}

// Only WSAEACCES is a denial: a missing family, a bad argument or an
// unreachable target fails without proving anything.
[[nodiscard]] QString Failure(int error) {
	return u"%1 (error %2)"_q.arg(
		(error == WSAEACCES) ? u"denied"_q : u"failed"_q,
		QString::number(error));
}

[[nodiscard]] QString StatusVerdict(NTSTATUS status) {
	return u"%1 (status 0x%2)"_q.arg(
		NT_SUCCESS(status)
			? u"ALLOWED"_q
			: (ULONG(status) == kStatusAccessDenied)
			? u"denied"_q
			: u"failed"_q,
		QString::number(uint(status), 16).toUpper());
}

[[nodiscard]] QByteArray Marker(const QString &mode, const char *phase) {
	return u"widget-helper-probe %1 %2 %3\n"_q.arg(
		QString::number(GetCurrentProcessId()),
		QString::fromLatin1(phase),
		mode).toUtf8();
}

[[nodiscard]] bool HasPort(const Target &target) {
	return (target.address.ss_family == AF_INET)
		? (reinterpret_cast<const sockaddr_in&>(target.address).sin_port != 0)
		: (reinterpret_cast<const sockaddr_in6&>(target.address).sin6_port
			!= 0);
}

[[nodiscard]] Socket CreateSocket(const Target &target) {
	return Socket(::socket(
		target.address.ss_family,
		target.tcp ? SOCK_STREAM : SOCK_DGRAM,
		target.tcp ? IPPROTO_TCP : IPPROTO_UDP));
}

[[nodiscard]] QString MakeNonblocking(SOCKET handle) {
	auto nonblocking = u_long(1);
	return (ioctlsocket(handle, FIONBIO, &nonblocking) == SOCKET_ERROR)
		? u"FIONBIO failed (error %1)"_q.arg(WSAGetLastError())
		: QString();
}

// A nonblocking connect judged by SO_ERROR once select() saw it complete:
// empty when connected, else the verdict. Writability alone, a pending
// WSAEWOULDBLOCK or a timeout is no success and no denial.
[[nodiscard]] QString Connect(SOCKET handle, const Target &target) {
	if (!::connect(
			handle,
			reinterpret_cast<const sockaddr*>(&target.address),
			target.length)) {
		return QString();
	}
	const auto error = WSAGetLastError();
	if (error != WSAEWOULDBLOCK) {
		return Failure(error);
	}
	auto writable = fd_set();
	auto failed = fd_set();
	FD_SET(handle, &writable);
	FD_SET(handle, &failed);
	auto timeout = timeval{ .tv_sec = kConnectWaitMs / 1000 };
	const auto ready = select(0, nullptr, &writable, &failed, &timeout);
	if (ready == SOCKET_ERROR) {
		return u"failed (select error %1)"_q.arg(WSAGetLastError());
	} else if (!ready) {
		return u"failed (no completion within %1 ms)"_q.arg(kConnectWaitMs);
	}
	auto pending = 0;
	auto size = int(sizeof(pending));
	if (getsockopt(
			handle,
			SOL_SOCKET,
			SO_ERROR,
			reinterpret_cast<char*>(&pending),
			&size) == SOCKET_ERROR) {
		return u"failed (SO_ERROR unreadable: error %1)"_q.arg(
			WSAGetLastError());
	} else if (pending) {
		return Failure(pending);
	} else if (!FD_ISSET(handle, &writable)) {
		return u"failed (SO_ERROR 0 without a completed connect)"_q;
	}
	return QString();
}

// Takes a nonblocking socket: connects TCP and sends |marker|, or sends
// |marker| in one UDP datagram, a send that would block failing at once
// (WSAEWOULDBLOCK, never a denial); the receiving fixture decides delivery.
[[nodiscard]] QString Exchange(
		SOCKET handle,
		const Target &target,
		const QByteArray &marker) {
	if (!target.tcp) {
		const auto sent = ::sendto(
			handle,
			marker.constData(),
			int(marker.size()),
			0,
			reinterpret_cast<const sockaddr*>(&target.address),
			target.length);
		return (sent == SOCKET_ERROR)
			? (u"sendto "_q + Failure(WSAGetLastError()))
			: u"sendto SENT (%1 bytes)"_q.arg(sent);
	}
	const auto connected = Connect(handle, target);
	if (!connected.isEmpty()) {
		return u"connect "_q + connected;
	}
	const auto sent = ::send(handle, marker.constData(), int(marker.size()), 0);
	return (sent == int(marker.size()))
		? u"connect CONNECTED, marker sent (%1 bytes)"_q.arg(sent)
		: u"connect CONNECTED, marker send failed (%1)"_q.arg(
			(sent == SOCKET_ERROR)
				? u"error %1"_q.arg(WSAGetLastError())
				: u"%1 of %2 bytes"_q.arg(sent).arg(marker.size()));
}

[[nodiscard]] AfdEa AfdEndpointEa(const SocketKind &kind) {
	const auto packet = AfdOpenPacket{
		.endpointFlags = (kind.type == SOCK_DGRAM)
			? (kAfdConnectionless | kAfdMessageOriented)
			: ULONG(0),
		.family = ULONG(kind.family),
		.type = ULONG(kind.type),
		.protocol = ULONG(kind.protocol),
	};
	const auto header = FullEaInformation{
		.nameLength = UCHAR(kAfdEaName.size()),
		.valueLength = USHORT(sizeof(packet) + sizeof(WCHAR)),
	};
	const auto value = offsetof(FullEaInformation, name)
		+ header.nameLength
		+ 1;

	// Sized as mswsock.dll sizes it; the bytes past the packet stay zero.
	const auto size = sizeof(header) + header.nameLength + header.valueLength;
	auto result = AfdEa{
		.storage = std::vector<ULONG>(
			(size + sizeof(ULONG) - 1) / sizeof(ULONG)),
		.size = ULONG(size),
		.endpointFlags = packet.endpointFlags,
	};
	const auto bytes = reinterpret_cast<char*>(result.storage.data());
	std::memcpy(bytes, &header, offsetof(FullEaInformation, name));
	std::memcpy(
		bytes + offsetof(FullEaInformation, name),
		kAfdEaName.data(),
		kAfdEaName.size());
	std::memcpy(
		bytes + value,
		&packet,
		offsetof(AfdOpenPacket, transportName));
	return result;
}

[[nodiscard]] NTSTATUS OpenEndpoint(
		const NtFunctions &nt,
		UNICODE_STRING &name,
		AfdEa &ea) {
	auto attributes = OBJECT_ATTRIBUTES();
	InitializeObjectAttributes(
		&attributes,
		&name,
		OBJ_CASE_INSENSITIVE,
		nullptr,
		nullptr);
	auto handle = HANDLE();
	auto io = IO_STATUS_BLOCK();
	const auto status = nt.create(
		&handle,
		GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
		&attributes,
		&io,
		nullptr,
		0,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN_IF,
		0,
		ea.storage.data(),
		ea.size);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	const auto guard = gsl::finally([&] {
		CloseHandle(handle);
	});
	return status;
}

NetworkProbe::NetworkProbe(const QStringList &arguments)
: _mode(arguments.value(1)) {
	parse(arguments);
}

NetworkProbe::~NetworkProbe() {
	close();
}

const QStringList &NetworkProbe::lines() const {
	return _lines;
}

void NetworkProbe::parse(const QStringList &arguments) {
	const auto port = arguments.value(5);
	if (!port.isEmpty()) {
		auto ok = false;
		const auto value = port.toInt(&ok);
		if (!ok || value < 0 || value > 65535) {
			malformed(u"loopback port \"%1\""_q.arg(port.left(kPortTextLimit)));
		} else if (value > 0) {
			_loopbackPort = value;
			for (const auto &host : { u"127.0.0.1"_q, u"[::1]"_q }) {
				for (const auto &protocol : { u"tcp"_q, u"udp"_q }) {
					addTarget(u"%1:%2:%3"_q.arg(
						protocol,
						host,
						QString::number(value)));
				}
			}
		}
	}
	for (const auto &argument : arguments.mid(6)) {
		if (argument == ConnectControlArgument()) {
			_connectControl = true;
		} else if (argument.startsWith(u"tcp:"_q)
			|| argument.startsWith(u"udp:"_q)) {
			addTarget(argument);
		} else {
			malformed(u"\"%1\""_q.arg(argument.left(kTargetTextLimit)));
		}
	}
}

void NetworkProbe::addTarget(const QString &text) {
	if (text.size() > kTargetTextLimit) {
		malformed(u"target \"%1...\": longer than %2 characters"_q.arg(
			text.left(kTargetTextLimit),
			QString::number(kTargetTextLimit)));
	} else {
		_targetTexts.push_back(text);
	}
}

void NetworkProbe::malformed(const QString &what) {
	_lines.push_back(u"argument: malformed "_q + what);
}

void NetworkProbe::startWinsock() {
	const auto bound = __HrLoadAllImportsForDll("WS2_32.dll");
	if (FAILED(bound)) {
		_lines.push_back(u"%1: harness Winsock: "
			"__HrLoadAllImportsForDll(WS2_32.dll) failed: 0x%2"_q.arg(
				QString::fromLatin1(kBeforeLowering),
				QString::number(uint(bound), 16)));
		return;
	}
	auto data = WSADATA();
	const auto started = WSAStartup(MAKEWORD(2, 2), &data);
	_winsock = !started;
	_lines.push_back(u"%1: harness Winsock: WS2_32 bound, WSAStartup(2.2) "
		"%2"_q.arg(
			QString::fromLatin1(kBeforeLowering),
			started ? u"failed (error %1)"_q.arg(started) : u"ok"_q));
}

void NetworkProbe::resolveTargets() {
	for (const auto &text : _targetTexts) {
		auto target = Target{
			.text = text,
			.tcp = text.startsWith(u"tcp:"_q),
		};
		auto address = text.mid(4).toStdWString();
		const auto read = [&](int family) {
			target.length = int(sizeof(target.address));
			return !WSAStringToAddressW(
				address.data(),
				family,
				nullptr,
				reinterpret_cast<sockaddr*>(&target.address),
				&target.length);
		};
		if (!read(AF_INET) && !read(AF_INET6)) {
			malformed(u"target \"%1\": WSAStringToAddressW failed: "
				"error %2"_q.arg(text, QString::number(WSAGetLastError())));
		} else if (!HasPort(target)) {
			malformed(u"target \"%1\": no port"_q.arg(text));
		} else if (int(_targets.size()) >= kTargetLimit) {
			malformed(u"target \"%1\": past the limit of %2 targets"_q.arg(
				text,
				QString::number(kTargetLimit)));
		} else {
			_targets.push_back(std::move(target));
		}
	}
}

void NetworkProbe::resolveNative() {
	if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
		_nt.create = reinterpret_cast<decltype(_nt.create)>(
			GetProcAddress(ntdll, "NtCreateFile"));
		_nt.init = reinterpret_cast<decltype(_nt.init)>(
			GetProcAddress(ntdll, "RtlInitUnicodeString"));
	}
	if (!_nt.create || !_nt.init) {
		_nt = NtFunctions();
		return;
	}
	_nt.init(&_afdName, kAfdEndpoint);
	for (const auto &kind : kSocketKinds) {
		_endpoints.push_back({ &kind, AfdEndpointEa(kind) });
	}
}

void NetworkProbe::attemptKinds(const QString &phase) {
	for (const auto &kind : kSocketKinds) {
		const auto socket = Socket(
			::socket(kind.family, kind.type, kind.protocol));
		const auto error = socket ? 0 : WSAGetLastError();
		_lines.push_back(u"%1: socket(%2): %3"_q.arg(
			phase,
			QString::fromLatin1(kind.name),
			error ? Failure(error) : u"ALLOWED"_q));
	}
}

void NetworkProbe::attemptTargets(
		const QString &phase,
		const char *markerPhase) {
	const auto marker = Marker(_mode, markerPhase);
	for (const auto &target : _targets) {
		const auto socket = CreateSocket(target);
		const auto error = socket ? 0 : WSAGetLastError();
		const auto nonblocking = socket
			? MakeNonblocking(socket.get())
			: QString();
		const auto use = target.tcp ? u"connect"_q : u"sendto"_q;
		const auto result = !socket
			? u"socket %1, %2 not tried, socket creation %3"_q.arg(
				Failure(error),
				use,
				(error == WSAEACCES) ? u"was denied"_q : u"failed"_q)
			: !nonblocking.isEmpty()
			? u"socket ALLOWED, %1, %2 not tried"_q.arg(nonblocking, use)
			: (u"socket ALLOWED, "_q
				+ Exchange(socket.get(), target, marker));
		_lines.push_back(u"%1: target %2: %3"_q.arg(phase, target.text, result));
	}
}

void NetworkProbe::attemptEndpoints(const QString &phase, bool control) {
	if (!_nt.create) {
		_lines.push_back(phase + u": AFD endpoint: not tried, "
			"ntdll's NtCreateFile or RtlInitUnicodeString is missing"_q);
		return;
	}
	for (auto &endpoint : _endpoints) {
		const auto status = OpenEndpoint(_nt, _afdName, endpoint.ea);
		auto line = u"%1: AFD endpoint %2: NtCreateFile(%3, EA %4 of %5 "
			"bytes, endpoint flags 0x%6): %7"_q.arg(
				phase,
				QString::fromLatin1(endpoint.kind->name),
				QString::fromWCharArray(kAfdEndpoint),
				QString::fromLatin1(kAfdEaName.data(), kAfdEaName.size()),
				QString::number(endpoint.ea.size),
				QString::number(endpoint.ea.endpointFlags, 16),
				StatusVerdict(status));
		if (control) {
			line += u"; EA bytes "_q + QString::fromLatin1(QByteArray(
				reinterpret_cast<const char*>(endpoint.ea.storage.data()),
				qsizetype(endpoint.ea.size)).toHex());
		}
		_lines.push_back(line);
	}
}

void NetworkProbe::preopen() {
	for (auto &target : _targets) {
		target.preopened = CreateSocket(target);
		const auto error = target.preopened ? 0 : WSAGetLastError();
		const auto nonblocking = target.preopened
			? MakeNonblocking(target.preopened.get())
			: QString();
		if (!nonblocking.isEmpty()) {
			target.preopened = Socket();
		}
		_lines.push_back(u"connect-control: %1: target %2: preopened "
			"socket %3"_q.arg(
				QString::fromLatin1(kBeforeLowering),
				target.text,
				error
					? Failure(error)
					: !nonblocking.isEmpty()
					? (u"ALLOWED, "_q + nonblocking + u", closed"_q)
					: u"ALLOWED, unconnected and nonblocking"_q));
	}
}

void NetworkProbe::attemptPreopened() {
	const auto phase = QString::fromLatin1(kAfterLowering);
	const auto marker = Marker(_mode, "connect-control");
	auto used = 0;
	for (auto &target : _targets) {
		if (!target.preopened) {
			_lines.push_back(u"connect-control: %1: target %2: not tried, "
				"no socket was preopened"_q.arg(phase, target.text));
			continue;
		}
		++used;
		const auto result = Exchange(target.preopened.get(), target, marker);
		const auto closed = target.preopened.close();
		_lines.push_back(u"connect-control: %1: target %2: %3; closesocket "
			"%4"_q.arg(
				phase,
				target.text,
				result,
				closed ? u"failed (error %1)"_q.arg(closed) : u"ok"_q));
	}
	_lines.push_back(u"connect-control: %1 preopened sockets used and closed "
		"before Ready; this run holds injected handles and is no ordinary "
		"helper"_q.arg(used));
}

void NetworkProbe::prepare() {
	const auto phase = QString::fromLatin1(kBeforeLowering);
	startWinsock();
	if (_winsock) {
		resolveTargets();
	}
	_lines.prepend(u"arguments: loopback port %1, %2 network targets, "
		"connect-control %3"_q.arg(
			_loopbackPort ? QString::number(_loopbackPort) : u"absent"_q,
			_winsock
				? QString::number(_targets.size())
				: (QString::number(_targetTexts.size()) + u" unresolved"_q),
			_connectControl ? u"yes"_q : u"no"_q));
	if (_winsock) {
		attemptKinds(phase);
		attemptTargets(phase, "before-lowering");
	} else {
		_lines.push_back(phase
			+ u": sockets and targets: not tried, Winsock is not available"_q);
	}
	resolveNative();
	attemptEndpoints(phase, true);
	if (_connectControl && _winsock) {
		preopen();
	}
}

void NetworkProbe::attempt() {
	const auto phase = QString::fromLatin1(kAfterLowering);
	if (_winsock) {
		attemptKinds(phase);
		attemptTargets(phase, "after-lowering");
	} else {
		_lines.push_back(phase
			+ u": sockets and targets: not tried, Winsock is not available"_q);
	}
	attemptEndpoints(phase, false);
	if (_connectControl && _winsock) {
		attemptPreopened();
	} else if (_connectControl) {
		_lines.push_back(
			u"connect-control: not tried, Winsock is not available"_q);
	}
}

void NetworkProbe::close() {
	for (auto &target : _targets) {
		target.preopened = Socket();
	}
	if (std::exchange(_winsock, false)) {
		WSACleanup();
	}
}

// The product probe takes its controls before the real lockdown, while the
// helper still impersonates, then the harness takes its own network
// controls; both make their attempts after the lockdown. The report goes to
// stderr, every socket the harness holds is closed before it returns, and
// the helper serves widgets.
[[nodiscard]] std::string LockDownWithProbe(
		const QStringList &arguments,
		const Fn<QString()> &lockdown) {
	if (!lockdown) {
		return "no lockdown callable";
	}
	const auto probe = Platform::WidgetHelper::PrepareProbe(
		NativeDirectory(arguments.value(2)));
	auto network = NetworkProbe(arguments);
	network.prepare();
	const auto error = lockdown();
	if (!error.isEmpty()) {
		return error.toStdString();
	}
	network.attempt();
	auto lines = probe().split(QChar('\n'));
	lines += network.lines();
	for (const auto &line : lines) {
		const auto utf8 = line.toUtf8();
		std::fprintf(stderr, "Widget helper probe: %s\n", utf8.constData());
	}
	return std::string();
}

#endif // Q_OS_WIN

} // namespace

bool IsWidgetHelperProbeMode(const QString &mode) {
	return (mode == ProbeMode()) || (mode == UnsandboxedMode());
}

QStringList WidgetHelperProbeArguments(const QString &mode) {
	return WidgetHelperProbeArguments(mode, {});
}

QStringList WidgetHelperProbeArguments(
		const QString &mode,
		const WidgetHelperProbeFixture &fixture) {
	if (!IsWidgetHelperProbeMode(mode)) {
		return {};
	}
	auto result = QStringList{
		mode,
		cWorkingDir(),
		QStandardPaths::writableLocation(
			QStandardPaths::GenericDataLocation),
	};
	if (!fixture.keychainAccount.isEmpty()
		|| fixture.loopbackPort > 0
		|| fixture.connectControl
		|| !fixture.networkTargets.isEmpty()) {
		result.push_back(fixture.keychainAccount);
		result.push_back(QString::number(std::max(fixture.loopbackPort, 0)));
		if (fixture.connectControl) {
			result.push_back(ConnectControlArgument());
		}
		result += fixture.networkTargets;
	}
	return result;
}

#ifdef Q_OS_WIN
std::string WidgetHelperLockdown(
		const QStringList &arguments,
		Fn<QString()> lockdown) {
	if (arguments.size() < 2) {
		return lockdown
			? lockdown().toStdString()
			: std::string("no lockdown callable");
	}
	const auto mode = arguments[1];
	if (!IsWidgetHelperProbeMode(mode)) {
		return "unknown widget helper mode \""
			+ mode.left(64).toStdString()
			+ "\"";
	}
	if (mode == ProbeMode()) {
		return LockDownWithProbe(arguments, lockdown);
	}
	return "the widget helper probe is not available on this platform";
}
#endif // Q_OS_WIN

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
