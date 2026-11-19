/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "_other/widget_helper_thread_mac.h"
#include "platform/posix/widget_channel_posix.h"

#include <gsl/gsl>

#include <xpc/xpc.h>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace WidgetHelper {
namespace {

constexpr auto kChannel = Platform::WidgetHelper::kChannelDescriptor;

auto Runner = std::function<void(std::vector<std::string>)>();
auto Arguments = std::vector<std::string>();
auto Peer = xpc_connection_t(nullptr);
auto KeepRequest = xpc_object_t(nullptr);
auto Adopted = false;

[[nodiscard]] std::string SystemError(const char *call, int error) {
	return std::string(call)
		+ " failed: "
		+ std::generic_category().message(error);
}

void Reply(
		xpc_connection_t peer,
		xpc_object_t message,
		const std::string &error) {
	const auto reply = xpc_dictionary_create_reply(message);
	if (!reply) {
		return;
	}
	if (!error.empty()) {
		xpc_dictionary_set_string(reply, "error", error.c_str());
	}
	xpc_connection_send_message(peer, reply);
	xpc_release(reply);
}

[[nodiscard]] std::string ReadArguments(
		xpc_object_t message,
		std::vector<std::string> &arguments) {
	const auto list = xpc_dictionary_get_value(message, "arguments");
	if (!list) {
		return {};
	} else if (xpc_get_type(list) != XPC_TYPE_ARRAY) {
		return "the arguments are not an array";
	}
	const auto count = xpc_array_get_count(list);
	for (auto i = size_t(); i != count; ++i) {
		const auto value = xpc_array_get_value(list, i);
		if (xpc_get_type(value) != XPC_TYPE_STRING) {
			return "an argument is not a string";
		}
		arguments.emplace_back(
			xpc_string_get_string_ptr(value),
			xpc_string_get_length(value));
	}
	return {};
}

[[nodiscard]] std::string PlaceDescriptors(xpc_object_t message) {
	const auto output = xpc_dictionary_dup_fd(message, "stderr");
	const auto channel = xpc_dictionary_dup_fd(message, "channel");
	auto placed = false;
	const auto release = gsl::finally([&] {
		const auto outputKept = placed
			&& (output == STDERR_FILENO || output == kChannel);
		if (output >= 0 && !outputKept) {
			close(output);
		}
		if (channel >= 0 && !(placed && channel == kChannel)) {
			close(channel);
		}
	});
	if (output < 0 || channel < 0) {
		return "the stderr or channel descriptor is missing";
	} else if (output != kChannel
		&& channel != kChannel
		&& fcntl(kChannel, F_GETFD) != -1) {
		return "descriptor " + std::to_string(kChannel) + " is already open";
	} else if (output != STDERR_FILENO && dup2(output, STDERR_FILENO) < 0) {
		return SystemError("dup2", errno);
	} else if (channel != kChannel && dup2(channel, kChannel) < 0) {
		return SystemError("dup2", errno);
	}
	placed = true;
	return {};
}

void *Serve(void *) {
	Runner(std::move(Arguments));
	return nullptr;
}

[[nodiscard]] std::string TakeChannel(xpc_object_t message) {
	auto arguments = std::vector<std::string>{ std::to_string(kChannel) };
	auto refused = ReadArguments(message, arguments);
	if (!refused.empty()) {
		return refused;
	}
	refused = PlaceDescriptors(message);
	if (!refused.empty()) {
		return refused;
	}
	Arguments = std::move(arguments);
	const auto result = StartDetachedThread(Serve, nullptr);
	if (result != 0) {
		close(kChannel);
		return SystemError("pthread_create", result);
	}
	return {};
}

void Handle(xpc_connection_t peer, xpc_object_t event) {
	const auto type = xpc_get_type(event);
	if (type == XPC_TYPE_ERROR) {
		std::_Exit(0);
	} else if (type != XPC_TYPE_DICTIONARY) {
		return;
	}
	const auto channel = xpc_dictionary_get_value(event, "channel");
	if (channel && Adopted) {
		Reply(peer, event, "the widget helper already serves a channel");
	} else if (channel) {
		const auto refused = TakeChannel(event);
		Reply(peer, event, refused);
		Adopted = refused.empty();
	} else if (Adopted && !KeepRequest) {
		KeepRequest = xpc_retain(event);
	} else {
		Reply(peer, event, {});
	}
}

void Accept(xpc_connection_t peer) {
	if (Peer) {
		xpc_connection_cancel(peer);
		return;
	}
	Peer = peer;
	xpc_connection_set_event_handler(peer, ^(xpc_object_t event) {
		Handle(peer, event);
	});
	xpc_connection_resume(peer);
}

} // namespace

void ServeService(std::function<void(std::vector<std::string>)> run) {
	Runner = std::move(run);
	xpc_main(Accept);
}

} // namespace WidgetHelper
