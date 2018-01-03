/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"

namespace Ui {

template <typename Widget, typename ParentType = RpWidget>
class Wrap;

namespace details {

struct UnwrapHelper {
	struct Large {
		char data[2];
	};
	static char Check(...);
	template <typename Widget, typename Parent>
	static Large Check(Wrap<Widget, Parent>*);
	template <typename Widget, typename Parent>
	static Large Check(const Wrap<Widget, Parent>*);

	template <typename Entity>
	static constexpr bool Is() {
		return sizeof(Check(std::declval<Entity>()))
			== sizeof(Large);
	}
	template <typename Entity>
	static auto Unwrap(Entity *entity, std::true_type) {
		return entity
			? entity->entity()
			: nullptr;
	}
	template <typename Entity>
	static Entity *Unwrap(Entity *entity, std::false_type) {
		return entity;
	}
	template <typename Entity>
	static auto Unwrap(Entity *entity) {
		return Unwrap(
			entity,
			std::integral_constant<bool, Is<Entity*>()>());
	}
};

} // namespace details

template <typename Widget>
class Wrap<Widget, RpWidget> : public RpWidget {
public:
	Wrap(QWidget *parent, object_ptr<Widget> child);

	Widget *wrapped() {
		return _wrapped;
	}
	const Widget *wrapped() const {
		return _wrapped;
	}
	auto entity() {
		return details::UnwrapHelper::Unwrap(wrapped());
	}
	auto entity() const {
		return details::UnwrapHelper::Unwrap(wrapped());
	}

	QMargins getMargins() const override {
		if (auto weak = wrapped()) {
			return weak->getMargins();
		}
		return RpWidget::getMargins();
	}
	int naturalWidth() const override {
		if (auto weak = wrapped()) {
			return weak->naturalWidth();
		}
		return RpWidget::naturalWidth();
	}

private:
	object_ptr<Widget> _wrapped;

};

template <typename Widget>
Wrap<Widget, RpWidget>::Wrap(QWidget *parent, object_ptr<Widget> child)
: RpWidget(parent)
, _wrapped(std::move(child)) {
	if (_wrapped) {
		resize(_wrapped->size());
		AttachParentChild(this, _wrapped);
		_wrapped->move(0, 0);
		_wrapped->alive()
			| rpl::on_done([this] {
				_wrapped->setParent(nullptr);
				_wrapped = nullptr;
				delete this;
			})
			| rpl::start(lifetime());
	}
}

template <typename Widget, typename ParentType>
class Wrap : public ParentType {
public:
	using ParentType::ParentType;

	Widget *wrapped() {
		return static_cast<Widget*>(ParentType::wrapped());
	}
	const Widget *wrapped() const {
		return static_cast<const Widget*>(ParentType::wrapped());
	}
	auto entity() {
		return details::UnwrapHelper::Unwrap(wrapped());
	}
	auto entity() const {
		return details::UnwrapHelper::Unwrap(wrapped());
	}

};

template <typename Widget>
class PaddingWrap;

template <>
class PaddingWrap<RpWidget> : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	PaddingWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		const style::margins &padding);

	PaddingWrap(
		QWidget *parent,
		const style::margins &padding)
		: PaddingWrap(parent, nullptr, padding) {
	}

	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void updateSize();

	int _innerWidth = 0;
	style::margins _padding;

};

template <typename Widget>
class PaddingWrap : public Wrap<Widget, PaddingWrap<RpWidget>> {
	using Parent = Wrap<Widget, PaddingWrap<RpWidget>>;

public:
	PaddingWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		const style::margins &padding)
	: Parent(parent, std::move(child), padding) {
	}

	PaddingWrap(QWidget *parent, const style::margins &padding)
	: Parent(parent, padding) {
	}

};

template <typename Widget>
class SlideWrap;

template <>
class SlideWrap<RpWidget> : public Wrap<PaddingWrap<RpWidget>> {
	using Parent = Wrap<PaddingWrap<RpWidget>>;

public:
	SlideWrap(QWidget *parent, object_ptr<RpWidget> child);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		const style::margins &padding);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		int duration);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		const style::margins &padding,
		int duration);

	void toggleAnimated(bool visible);
	void toggleFast(bool visible);

	void showAnimated() {
		toggleAnimated(true);
	}
	void hideAnimated() {
		toggleAnimated(false);
	}

	void showFast() {
		toggleFast(true);
	}
	void hideFast() {
		toggleFast(false);
	}

	bool animating() const {
		return _slideAnimation.animating();
	}
	void finishAnimations();

	QMargins getMargins() const override;

	bool isHiddenOrHiding() const {
		return !_visible;
	}

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void animationStep();

	bool _visible = true;
	Animation _slideAnimation;
	int _duration = 0;

};

template <typename Widget>
class SlideWrap : public Wrap<PaddingWrap<Widget>, SlideWrap<RpWidget>> {
	using Parent = Wrap<PaddingWrap<Widget>, SlideWrap<RpWidget>>;

public:
	SlideWrap(QWidget *parent, object_ptr<Widget> child)
	: Parent(parent, std::move(child)) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		const style::margins &padding)
	: Parent(parent, std::move(child), padding) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		int duration)
	: Parent(parent, std::move(child), duration) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		const style::margins &padding,
		int duration)
	: Parent(parent, std::move(child), padding, duration) {
	}

};

} // namespace Ui
