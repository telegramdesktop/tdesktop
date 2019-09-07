/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "ui/wrap/vertical_layout.h"

#include <tuple>

namespace st {
extern const style::margins &boxRowPadding;
} // namespace st

class GenericBox : public BoxContent {
public:
	// InitMethod::operator()(not_null<GenericBox*> box, InitArgs...)
	// init(box, args...)
	template <
		typename InitMethod,
		typename ...InitArgs,
		typename = decltype(std::declval<std::decay_t<InitMethod>>()(
			std::declval<not_null<GenericBox*>>(),
			std::declval<std::decay_t<InitArgs>>()...))>
	GenericBox(
		QWidget*,
		InitMethod &&init,
		InitArgs &&...args);

	void setWidth(int width) {
		_width = width;
	}
	void setFocusCallback(Fn<void()> callback) {
		_focus = callback;
	}

	int rowsCount() const {
		return _content->count();
	}

	template <
		typename Widget,
		typename = std::enable_if_t<
		std::is_base_of_v<RpWidget, Widget>>>
	Widget *insertRow(
			int atPosition,
			object_ptr<Widget> &&child,
			const style::margins &margin = st::boxRowPadding) {
		return _content->insert(
			atPosition,
			std::move(child),
			margin);
	}

	template <
		typename Widget,
		typename = std::enable_if_t<
		std::is_base_of_v<RpWidget, Widget>>>
	Widget *addRow(
			object_ptr<Widget> &&child,
			const style::margins &margin = st::boxRowPadding) {
		return _content->add(std::move(child), margin);
	}

	void addSkip(int height);

	void setInnerFocus() override {
		if (_focus) {
			_focus();
		}
	}

protected:
	void prepare() override;

private:
	template <typename InitMethod, typename ...InitArgs>
	struct Initer {
		template <
			typename OtherMethod,
			typename ...OtherArgs,
			typename = std::enable_if_t<
				std::is_constructible_v<InitMethod, OtherMethod&&>>>
		Initer(OtherMethod &&method, OtherArgs &&...args);

		void operator()(not_null<GenericBox*> box);

		template <std::size_t... I>
		void call(
			not_null<GenericBox*> box,
			std::index_sequence<I...>);

		InitMethod method;
		std::tuple<InitArgs...> args;
	};

	template <typename InitMethod, typename ...InitArgs>
	auto MakeIniter(InitMethod &&method, InitArgs &&...args)
		-> Initer<std::decay_t<InitMethod>, std::decay_t<InitArgs>...>;

	FnMut<void(not_null<GenericBox*>)> _init;
	Fn<void()> _focus;
	object_ptr<Ui::VerticalLayout> _content;
	int _width = 0;

};

template <typename InitMethod, typename ...InitArgs>
template <typename OtherMethod, typename ...OtherArgs, typename>
GenericBox::Initer<InitMethod, InitArgs...>::Initer(
	OtherMethod &&method,
	OtherArgs &&...args)
: method(std::forward<OtherMethod>(method))
, args(std::forward<OtherArgs>(args)...) {
}

template <typename InitMethod, typename ...InitArgs>
inline void GenericBox::Initer<InitMethod, InitArgs...>::operator()(
		not_null<GenericBox*> box) {
	call(box, std::make_index_sequence<sizeof...(InitArgs)>());
}

template <typename InitMethod, typename ...InitArgs>
template <std::size_t... I>
inline void GenericBox::Initer<InitMethod, InitArgs...>::call(
		not_null<GenericBox*> box,
		std::index_sequence<I...>) {
	std::invoke(method, box, std::get<I>(std::move(args))...);
}

template <typename InitMethod, typename ...InitArgs>
inline auto GenericBox::MakeIniter(InitMethod &&method, InitArgs &&...args)
-> Initer<std::decay_t<InitMethod>, std::decay_t<InitArgs>...> {
	return {
		std::forward<InitMethod>(method),
		std::forward<InitArgs>(args)...
	};
}

template <typename InitMethod, typename ...InitArgs, typename>
inline GenericBox::GenericBox(
	QWidget*,
	InitMethod &&init,
	InitArgs &&...args)
: _init(
	MakeIniter(
		std::forward<InitMethod>(init),
		std::forward<InitArgs>(args)...))
, _content(this) {
}
