/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/layers/standalone_layer_stack.h"

#include "base/event_filter.h"
#include "ui/layers/box_content.h"
#include "ui/layers/box_layer_widget.h"
#include "ui/layers/show.h"
#include "ui/widgets/separate_panel.h"
#include "base/debug_log.h"
#include "styles/style_layers.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Ui {
namespace {

class StandaloneShow final : public Show {
public:
	explicit StandaloneShow(base::weak_ptr<StandaloneLayerStack> weak);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

private:
	const base::weak_ptr<StandaloneLayerStack> _stack;

};

StandaloneShow::StandaloneShow(base::weak_ptr<StandaloneLayerStack> weak)
: _stack(std::move(weak)) {
}

void StandaloneShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const {
	using UniqueLayer = std::unique_ptr<LayerWidget>;
	using ObjectBox = object_ptr<BoxContent>;
	const auto stack = _stack.get();
	if (!stack) {
		return;
	}
	if (auto box = std::get_if<ObjectBox>(&layer)) {
		stack->showBox(std::move(*box), options, animated);
	} else if (std::get_if<UniqueLayer>(&layer)) {
		LOG(("StandaloneLayerStack: showLayer(LayerWidget) "
			"is not supported, ignoring."));
	} else {
		stack->hideLayers(animated);
	}
}

not_null<QWidget*> StandaloneShow::toastParent() const {
	const auto stack = _stack.get();
	const auto parent = stack ? stack->currentPanel() : nullptr;
	Ensures(parent != nullptr);
	return parent;
}

bool StandaloneShow::valid() const {
	return (_stack.get() != nullptr);
}

StandaloneShow::operator bool() const {
	return valid();
}

} // namespace

StandaloneLayerStack::StandaloneLayerStack() = default;

StandaloneLayerStack::~StandaloneLayerStack() {
	for (auto &entry : base::take(_entries)) {
		if (entry.box) {
			entry.box->setClosing();
		}
	}
}

void StandaloneLayerStack::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	if (options & LayerOption::CloseOther) {
		hideLayers(anim::type::instant);
	} else if (!_entries.empty()) {
		_entries.back().panel->hideForStacking();
	}
	auto panel = base::make_unique_q<SeparatePanel>(SeparatePanelArgs{
		.anchorGeometry = _anchorGeometry,
		.transientParent = _transientParent,
	});
	panel->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	panel->setAttribute(Qt::WA_DeleteOnClose, false);
	panel->setTitleHeight(0);
	panel->setCloseAllowed(false);

	const auto rawPanel = panel.get();
	auto layerWidget = base::make_unique_q<BoxLayerWidget>(
		rawPanel,
		this,
		std::move(box));
	const auto rawBox = layerWidget.get();

	rawPanel->setInnerSize(rawBox->size().isEmpty()
		? QSize(st::boxWideWidth, 1)
		: rawBox->size());

	rawBox->sizeValue(
	) | rpl::on_next([rawPanel](QSize size) {
		if (!size.isEmpty()) {
			rawPanel->setInnerSize(size);
		}
	}, rawBox->lifetime());

	rawBox->setClosedCallback([=] {
		closeEntry(rawPanel);
	});
	rawPanel->closeRequests(
	) | rpl::on_next([=] {
		closeEntry(rawPanel);
	}, rawPanel->lifetime());
	rawPanel->closeEvents(
	) | rpl::on_next([=] {
		closeEntry(rawPanel);
	}, rawPanel->lifetime());

	rawPanel->showInner(std::move(layerWidget));
	rawPanel->showAndActivate();

	_entries.push_back({ std::move(panel), rawBox });
	_boxAdded.fire({});
}

void StandaloneLayerStack::hideLayers(anim::type animated) {
	auto taken = base::take(_entries);
	for (auto &entry : taken) {
		if (entry.box) {
			entry.box->setClosing();
		}
		entry.panel->hideGetDuration();
		_boxClosed.fire({});
	}
}

void StandaloneLayerStack::setAnchor(
		std::optional<QRect> geometry,
		std::optional<QSize> outerSize,
		Platform::ForeignParent transientParent) {
	_anchorGeometry = std::move(geometry);
	_anchorOuterSize = std::move(outerSize);
	_transientParent = std::move(transientParent);
	for (const auto &entry : _entries) {
		entry.panel->setAnchorData(_anchorGeometry, _transientParent);
	}
}

ShowFactory StandaloneLayerStack::showFactory() {
	const auto weak = base::make_weak(this);
	return [weak]() -> ShowPtr {
		return std::make_shared<StandaloneShow>(weak);
	};
}

std::optional<QSize> StandaloneLayerStack::layerOuterSize() {
	if (_anchorOuterSize) {
		return _anchorOuterSize;
	}
	if (_anchorGeometry) {
		return _anchorGeometry->size();
	}
	if (const auto screen = QGuiApplication::primaryScreen()) {
		return screen->availableGeometry().size();
	}
	return std::nullopt;
}

QWidget *StandaloneLayerStack::currentPanel() const {
	return _entries.empty() ? nullptr : _entries.back().panel.get();
}

void StandaloneLayerStack::closeEntry(SeparatePanel *panel) {
	const auto i = ranges::find_if(_entries, [&](const Entry &entry) {
		return entry.panel.get() == panel;
	});
	if (i == end(_entries)) {
		return;
	}
	const auto wasTop = (i + 1 == end(_entries));
	auto entry = std::move(*i);
	_entries.erase(i);
	if (entry.box) {
		entry.box->setClosing();
	}
	entry.panel->hideGetDuration();
	_boxClosed.fire({});
	if (wasTop && !_entries.empty()) {
		_entries.back().panel->setAnchorData(
			_anchorGeometry,
			_transientParent);
		_entries.back().panel->showAndActivate();
	}
}

} // namespace Ui
