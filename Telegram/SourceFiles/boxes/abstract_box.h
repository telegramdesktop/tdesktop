/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_layer_widget.h"

class Painter;

namespace style {
struct RoundButton;
struct IconButton;
struct ScrollArea;
} // namespace style

namespace Ui {
class RoundButton;
class IconButton;
class ScrollArea;
class FlatLabel;
class FadeShadow;
} // namespace Ui

// Legacy global method.
namespace Ui {
namespace internal {

void showBox(
	object_ptr<BoxContent> content,
	Ui::LayerOptions options,
	anim::type animated);

} // namespace internal

template <typename BoxType>
QPointer<BoxType> show(
		object_ptr<BoxType> content,
		Ui::LayerOptions options = Ui::LayerOption::CloseOther,
		anim::type animated = anim::type::normal) {
	auto result = QPointer<BoxType>(content.data());
	internal::showBox(std::move(content), options, animated);
	return result;
}

void hideLayer(anim::type animated = anim::type::normal);
bool isLayerShown();

} // namespace Ui
