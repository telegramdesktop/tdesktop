/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// Use of the code is permitted as long as links to the original source are maintained.
// Author: https://github.com/DyingLay

AhiGram: Custom badge tooltip widget
*/

#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace style {
struct ImportantTooltip;
} // namespace style

namespace AhiGram {
namespace UI {

class CustomBadgeTooltip final : public Ui::RpWidget {
public:
    CustomBadgeTooltip(
        not_null<QWidget*> parent,
        const QString &title,
        const QString &description,
        const style::icon *badgeIcon,
        not_null<QWidget*> pointTo);

    void fade(bool shown);
    void finishAnimating();
	
    [[nodiscard]] rpl::producer<bool> shownValue() const;
    [[nodiscard]] crl::time hideDuration() const;
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    void setupGeometry(not_null<QWidget*> pointTo);
	
    const style::ImportantTooltip &_st;
    const QString _title;
    const QString _description;
    const style::icon *_badgeIcon = nullptr;

    QFont _titleFont;
    QFont _descFont;
    QSize _inner;
    QSize _outer;
    QSize _full;

    int _stroke = 0;
    int _skip = 0;
    int _arrowCenterX = 0;

    Ui::Animations::Simple _showAnimation;
    bool _shown = false;
    bool _isAnimating = false;
    rpl::variable<bool> _shownVariable = false;
};
[[nodiscard]] CustomBadgeTooltip* CreateImportantTooltip(
    not_null<QWidget*> parent,
    const QString &title,
    const QString &description,
    const style::icon *badgeIcon,
    not_null<QWidget*> pointTo,
    crl::time hideAfter = 0);
} // namespace UI
} // namespace AhiGram