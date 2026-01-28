/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/text/text.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "data/data_peer_colors.h"

namespace Ui {
namespace Text {
struct MarkedContext;
} // namespace Text
class ChatStyle;
struct ColorCollectible;

class ColorSample final : public AbstractButton {
public:
	ColorSample(
		not_null<QWidget*> parent,
		Fn<Ui::Text::MarkedContext()> contextProvider,
		Fn<TextWithEntities(uint64)> emojiProvider,
		std::shared_ptr<ChatStyle> style,
		rpl::producer<uint8> colorIndex,
		rpl::producer<std::shared_ptr<ColorCollectible>> collectible,
		const QString &name);
	ColorSample(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatStyle> style,
		uint8 colorIndex,
		bool selected);
	ColorSample(
		not_null<QWidget*> parent,
		Fn<Data::ColorProfileSet(uint8)> profileProvider,
		uint8 colorIndex,
		bool selected);

	[[nodiscard]] uint8 index() const;

	void setSelected(bool selected);
	void setCutoutPadding(int padding);
	void setForceCircle(bool force);

private:
	void paintEvent(QPaintEvent *e) override;

	std::shared_ptr<ChatStyle> _style;
	Text::String _name;
	uint8 _index = 0;
	std::shared_ptr<ColorCollectible> _collectible;
	Animations::Simple _selectAnimation;
	bool _selected = false;
	bool _simple = false;
	Fn<Data::ColorProfileSet(uint8)> _profileProvider;
	int _cutoutPadding = 0;
	bool _forceCircle = false;

};

class ColorSelector final : public RpWidget {
public:
	ColorSelector(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatStyle> style,
		rpl::producer<std::vector<uint8>> indices,
		rpl::producer<uint8> index,
		Fn<void(uint8)> callback);
	ColorSelector(
		not_null<QWidget*> parent,
		const std::vector<uint8> &indices,
		uint8 index,
		Fn<void(uint8)> callback,
		Fn<Data::ColorProfileSet(uint8)> profileProvider);

	void updateSelection(uint8 newIndex);

private:
	void fillFrom(std::vector<uint8> indices);
	void setupSelectionTracking();

	int resizeGetHeight(int newWidth) override;

	std::shared_ptr<ChatStyle> _style;
	std::vector<std::unique_ptr<ColorSample>> _samples;
	Fn<void(uint8)> _callback;
	rpl::variable<uint8> _index;
	Fn<Data::ColorProfileSet(uint8)> _profileProvider;
	bool _isProfileMode = false;

};

} // namespace Ui
