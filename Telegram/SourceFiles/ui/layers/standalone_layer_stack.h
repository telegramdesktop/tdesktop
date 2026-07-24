/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/layers/layer_widget.h"

#include <QtCore/QRect>
#include <QtCore/QSize>

#include <optional>
#include <vector>

namespace anim {
enum class type : uchar;
} // namespace anim

namespace Ui {

class BoxContent;
class BoxLayerWidget;
class SeparatePanel;

// Shows each BoxContent in its own top-level frameless SeparatePanel,
// using BoxLayerWidget as the panel body. Behaves like a layer stack:
// each new box hides the previous one, closing a box reveals the box
// shown before it.
class StandaloneLayerStack final
	: public LayerStackDelegate
	, public base::has_weak_ptr {
public:
	StandaloneLayerStack();
	~StandaloneLayerStack();

	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) override;
	void hideLayers(anim::type animated) override;
	void setAnchor(
		std::optional<QRect> geometry,
		std::optional<QSize> outerSize,
		Platform::ForeignParent transientParent);
	ShowFactory showFactory() override;
	std::optional<QSize> layerOuterSize() override;
	bool centerWithinOuter() override {
		return false;
	}
	bool dragByTitle() override {
		return true;
	}

	// Top (currently visible) panel, if any. Used as toast parent.
	[[nodiscard]] QWidget *currentPanel() const;

	[[nodiscard]] rpl::producer<> boxAdded() const {
		return _boxAdded.events();
	}
	[[nodiscard]] rpl::producer<> boxClosed() const {
		return _boxClosed.events();
	}

private:
	struct Entry {
		base::unique_qptr<SeparatePanel> panel;
		BoxLayerWidget *box = nullptr;
	};

	void closeEntry(SeparatePanel *panel);
	void hideAllPanels();

	std::vector<Entry> _entries;
	std::optional<QRect> _anchorGeometry;
	std::optional<QSize> _anchorOuterSize;
	Platform::ForeignParent _transientParent;
	rpl::event_stream<> _boxAdded;
	rpl::event_stream<> _boxClosed;

};

} // namespace Ui
