/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_progress.h"

#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_export.h"

namespace Export {
namespace View {

class ProgressWidget::Row : public Ui::RpWidget {
public:
	Row(QWidget *parent, Content::Row &&data);

	void updateData(Content::Row &&data);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	struct Instance {
		base::unique_qptr<Ui::FadeWrap<Ui::FlatLabel>> label;
		base::unique_qptr<Ui::FadeWrap<Ui::FlatLabel>> info;

		float64 value = 0.;
		Animation progress;

		bool hiding = true;
		Animation opacity;
	};

	void fillCurrentInstance();
	void hideCurrentInstance();
	void setInstanceProgress(Instance &instance, float64 progress);
	void toggleInstance(Instance &data, bool shown);
	void instanceOpacityCallback(QPointer<Ui::FlatLabel> label);
	void removeOldInstance(QPointer<Ui::FlatLabel> label);
	void paintInstance(Painter &p, const Instance &data);

	void updateControlsGeometry(int newWidth);
	void updateInstanceGeometry(const Instance &instance, int newWidth);

	Content::Row _data;
	Instance _current;
	std::vector<Instance> _old;

};

ProgressWidget::Row::Row(QWidget *parent, Content::Row &&data)
: RpWidget(parent)
, _data(std::move(data)) {
	fillCurrentInstance();
}

void ProgressWidget::Row::updateData(Content::Row &&data) {
	const auto wasId = _data.id;
	const auto nowId = data.id;
	_data = std::move(data);
	if (nowId.isEmpty()) {
		hideCurrentInstance();
	} else if (wasId.isEmpty()) {
		fillCurrentInstance();
	} else {
		_current.label->entity()->setText(_data.label);
		_current.info->entity()->setText(_data.info);
		setInstanceProgress(_current, _data.progress);
		if (nowId != wasId) {
			_current.progress.finish();
		}
	}
	updateControlsGeometry(width());
	update();
}

void ProgressWidget::Row::fillCurrentInstance() {
	_current.label = base::make_unique_q<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			_data.label,
			Ui::FlatLabel::InitType::Simple,
			st::exportProgressLabel));
	_current.info = base::make_unique_q<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			_data.info,
			Ui::FlatLabel::InitType::Simple,
			st::exportProgressInfoLabel));
	_current.label->hide(anim::type::instant);
	_current.info->hide(anim::type::instant);

	setInstanceProgress(_current, _data.progress);
	toggleInstance(_current, true);
	if (_data.id == "main") {
		_current.opacity.finish();
		_current.label->finishAnimating();
		_current.info->finishAnimating();
	}
}

void ProgressWidget::Row::hideCurrentInstance() {
	if (!_current.label) {
		return;
	}
	setInstanceProgress(_current, 1.);
	toggleInstance(_current, false);
	_old.push_back(std::move(_current));
}

void ProgressWidget::Row::setInstanceProgress(
		Instance &instance,
		float64 progress) {
	if (_current.value < progress) {
		_current.progress.start(
			[=] { update(); },
			_current.value,
			progress,
			st::exportProgressDuration,
			anim::sineInOut);
	} else if (_current.value > progress) {
		_current.progress.finish();
	}
	_current.value = progress;
}

void ProgressWidget::Row::toggleInstance(Instance &instance, bool shown) {
	Expects(instance.label != nullptr);

	if (instance.hiding != shown) {
		return;
	}
	const auto label = make_weak(instance.label->entity());
	instance.opacity.start(
		[=] { instanceOpacityCallback(label); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::exportProgressDuration);
	instance.hiding = !shown;
	_current.label->toggle(shown, anim::type::normal);
	_current.info->toggle(shown, anim::type::normal);
}

void ProgressWidget::Row::instanceOpacityCallback(
		QPointer<Ui::FlatLabel> label) {
	update();
	const auto i = ranges::find(_old, label, [](const Instance &instance) {
		return make_weak(instance.label->entity());
	});
	if (i != end(_old) && i->hiding && !i->opacity.animating()) {
		crl::on_main(this, [=] {
			removeOldInstance(label);
		});
	}
}

void ProgressWidget::Row::removeOldInstance(QPointer<Ui::FlatLabel> label) {
	const auto i = ranges::find(_old, label, [](const Instance &instance) {
		return make_weak(instance.label->entity());
	});
	if (i != end(_old)) {
		_old.erase(i);
	}
}

int ProgressWidget::Row::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return st::exportProgressRowHeight;
}

void ProgressWidget::Row::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto thickness = st::exportProgressWidth;
	const auto top = height() - thickness;
	p.fillRect(0, top, width(), thickness, st::shadowFg);

	for (const auto &instance : _old) {
		paintInstance(p, instance);
	}
	paintInstance(p, _current);
}

void ProgressWidget::Row::paintInstance(Painter &p, const Instance &data) {
	const auto opacity = data.opacity.current(data.hiding ? 0. : 1.);

	if (!opacity) {
		return;
	}
	p.setOpacity(opacity);

	const auto thickness = st::exportProgressWidth;
	const auto top = height() - thickness;
	const auto till = qRound(data.progress.current(data.value) * width());
	if (till > 0) {
		p.fillRect(0, top, till, thickness, st::exportProgressFg);
	}
	if (till < width()) {
		const auto left = width() - till;
		p.fillRect(till, top, left, thickness, st::exportProgressBg);
	}
}

void ProgressWidget::Row::updateControlsGeometry(int newWidth) {
	updateInstanceGeometry(_current, newWidth);
	for (const auto &instance : _old) {
		updateInstanceGeometry(instance, newWidth);
	}
}

void ProgressWidget::Row::updateInstanceGeometry(
		const Instance &instance,
		int newWidth) {
	if (!instance.label) {
		return;
	}
	instance.info->resizeToNaturalWidth(newWidth);
	instance.label->resizeToWidth(newWidth - instance.info->width());
	instance.info->moveToRight(0, 0, newWidth);
	instance.label->moveToLeft(0, 0, newWidth);
}

ProgressWidget::ProgressWidget(
	QWidget *parent,
	rpl::producer<Content> content)
: RpWidget(parent)
, _body(this) {
	widthValue(
	) | rpl::start_with_next([=](int width) {
		_body->resizeToWidth(width);
		_body->moveToLeft(0, 0);
	}, _body->lifetime());

	_about = _body->add(
		object_ptr<Ui::FlatLabel>(
			this,
			lang(lng_export_progress),
			Ui::FlatLabel::InitType::Simple,
			st::exportAboutLabel),
		st::exportAboutPadding);

	std::move(
		content
	) | rpl::start_with_next([=](Content &&content) {
		updateState(std::move(content));
	}, lifetime());

	_cancel = base::make_unique_q<Ui::RoundButton>(
		this,
		langFactory(lng_export_stop),
		st::exportCancelButton);
	setupBottomButton(_cancel.get());
}

rpl::producer<> ProgressWidget::cancelClicks() const {
	return _cancel
		? (_cancel->clicks() | rpl::map([] { return rpl::empty_value(); }))
		: (rpl::never<>() | rpl::type_erased());
}

rpl::producer<> ProgressWidget::doneClicks() const {
	return _doneClicks.events();
}

void ProgressWidget::setupBottomButton(not_null<Ui::RoundButton*> button) {
	button->show();

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		button->move(
			(size.width() - button->width()) / 2,
			(size.height() - st::exportCancelBottom - button->height()));
	}, button->lifetime());
}

void ProgressWidget::updateState(Content &&content) {
	if (!content.rows.empty() && content.rows[0].id == Content::kDoneId) {
		showDone();
	}

	auto index = 0;
	for (auto &row : content.rows) {
		if (index < _rows.size()) {
			_rows[index]->updateData(std::move(row));
		} else {
			_rows.push_back(_body->insert(
				index,
				object_ptr<Row>(this, std::move(row)),
				st::exportProgressRowPadding));
		}
		++index;
	}
	for (const auto count = _rows.size(); index != count; ++index) {
		_rows[index]->updateData(Content::Row());
	}
}

void ProgressWidget::showDone() {
	_cancel = nullptr;
	_about->setText(lang(lng_export_about_done));
	_done = base::make_unique_q<Ui::RoundButton>(
		this,
		langFactory(lng_export_done),
		st::exportDoneButton);
	const auto desired = std::min(
		st::exportDoneButton.font->width(lang(lng_export_done).toUpper())
		+ st::exportDoneButton.height
		- st::exportDoneButton.font->height,
		st::exportPanelSize.width() - 2 * st::exportCancelBottom);
	if (_done->width() < desired) {
		_done->setFullWidth(desired);
	}
	_done->clicks(
	) | rpl::map([] {
		return rpl::empty_value();
	}) | rpl::start_to_stream(_doneClicks, _done->lifetime());
	setupBottomButton(_done.get());
}

ProgressWidget::~ProgressWidget() = default;

} // namespace View
} // namespace Export
