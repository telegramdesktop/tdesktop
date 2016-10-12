#pragma once

namespace Ui {

class PlainShadow : public TWidget {
public:
	PlainShadow(QWidget *parent, const style::color &color) : TWidget(parent), _color(color) {
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter(this).fillRect(e->rect(), _color->b);
	}

private:
	const style::color &_color;

};

class ToggleableShadow : public TWidget {
public:
	ToggleableShadow(QWidget *parent, const style::color &color) : TWidget(parent), _color(color) {
	}

	enum class Mode {
		Shown,
		ShownFast,
		Hidden,
		HiddenFast
	};
	void setMode(Mode mode);

	bool isFullyShown() const {
		return _shown && !_a_opacity.animating();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::color &_color;
	FloatAnimation _a_opacity;
	bool _shown = true;

};

} // namespace Ui
