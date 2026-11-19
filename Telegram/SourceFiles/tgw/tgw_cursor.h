/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace litehtml {
class document;
class element;
} // namespace litehtml

namespace twlite {
class Renderer;
} // namespace twlite

namespace Tgw {

// A point query, as twlite::Renderer::cursor() is the last delivered move's.
class CursorStyles final {
public:
	explicit CursorStyles(double dpr);
	~CursorStyles();

	[[nodiscard]] bool pointer(
		twlite::Renderer &renderer,
		std::int64_t hit,
		std::int64_t hover,
		std::int64_t active);
	[[nodiscard]] std::optional<bool> hold(
		twlite::Renderer &renderer,
		std::int64_t hit,
		std::int64_t hover);

private:
	class Container;

	void sync(
		twlite::Renderer &renderer,
		std::int64_t hover,
		std::int64_t active);
	[[nodiscard]] bool refresh(twlite::Renderer &renderer);
	void rebuild(double width, int height);
	void index();

	const std::unique_ptr<Container> _container;
	std::string _master;
	std::shared_ptr<litehtml::document> _document;
	std::unordered_map<
		std::int64_t,
		std::shared_ptr<litehtml::element>> _elements;
	std::string _html;
	double _width = 0.;
	int _height = 0;
	int _builds = 0;
	std::int64_t _hover = 0;
	std::int64_t _active = 0;

};

} // namespace Tgw
