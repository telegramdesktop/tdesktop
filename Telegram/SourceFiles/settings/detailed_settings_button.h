#pragma once

#include "settings/settings_common.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace style {
struct DetailedSettingsButtonStyle;
} // namespace style

class DetailedSettingsButton final : public Ui::RippleButton {
public:
	DetailedSettingsButton(
		QWidget *parent,
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		Settings::IconDescriptor icon,
		rpl::producer<bool> toggled,
		const style::DetailedSettingsButtonStyle &rowStyle);

	QString accessibilityName() override;
	Ui::AccessibilityState accessibilityState() const override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
	QAccessible::Role accessibilityRole() override;
#endif

	[[nodiscard]] bool toggled() const;
	[[nodiscard]] rpl::producer<bool> toggledChanges() const;
	[[nodiscard]] rpl::producer<not_null<QEvent*>> clickAreaEvents() const;
	void setToggleLocked(bool locked);
	void finishAnimating();

protected:
	void onStateChanged(State was, StateChangeSource source) override;
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void refreshLayout();
	[[nodiscard]] int titleTop() const;
	[[nodiscard]] int titleHeight() const;
	[[nodiscard]] int descriptionTopValue() const;
	[[nodiscard]] int firstDescriptionLineBottom() const;
	[[nodiscard]] int textAvailableWidth(int outerw) const;
	[[nodiscard]] QRect toggleRect() const;
	[[nodiscard]] QRect iconAreaRect() const;
	[[nodiscard]] QRect iconRect() const;
	[[nodiscard]] QRect iconForegroundRect(QRect iconRect) const;
	[[nodiscard]] int iconSize() const;
	void paintIcon(QPainter &p) const;

	const style::DetailedSettingsButtonStyle &_style;
	Ui::Text::String _title;
	Ui::Text::String _description;
	std::unique_ptr<Ui::ToggleView> _toggle;
	const style::icon *_iconForeground = nullptr;
	const style::color *_iconBackground = nullptr;
	const std::optional<QBrush> _iconBackgroundBrush;
	int _titleWidth = 0;
	int _descriptionWidth = 0;
	int _descriptionTop = 0;
	int _descriptionHeight = 0;
	bool _toggleLocked = false;

};

[[nodiscard]] not_null<DetailedSettingsButton*> AddDetailedSettingsButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> title,
	rpl::producer<QString> description,
	Settings::IconDescriptor icon,
	rpl::producer<bool> toggled,
	const style::DetailedSettingsButtonStyle &rowStyle);
