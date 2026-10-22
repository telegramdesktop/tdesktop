/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace litehtml {
class document;
class element;
} // namespace litehtml

namespace twlite {
class Renderer;
} // namespace twlite

namespace Tgw {

// Stopgap until twlite::Renderer exposes the cursor its document computes.
class CursorStyles final {
public:
	CursorStyles(
		double dpr,
		const std::vector<std::pair<std::string, std::string>> &variables);
	~CursorStyles();

	[[nodiscard]] bool pointer(
		twlite::Renderer &renderer,
		int64 hit,
		int64 hover,
		int64 active);

private:
	class Container;

	[[nodiscard]] bool refresh(twlite::Renderer &renderer);
	void rebuild(double width, int height);
	void index();

	const std::unique_ptr<Container> _container;
	const std::string _master;
	std::shared_ptr<litehtml::document> _document;
	std::unordered_map<int64, std::shared_ptr<litehtml::element>> _elements;
	std::string _html;
	double _width = 0.;
	int _height = 0;
	int _builds = 0;
	int64 _hover = 0;
	int64 _active = 0;

};

} // namespace Tgw
