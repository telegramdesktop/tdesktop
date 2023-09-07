/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

class BoxContent;

namespace Window {
    class Controller;

    class SessionController;
} // namespace Window

namespace Settings {
    class Rabbit : public Section<Rabbit> {
    public:
        Rabbit(QWidget *parent, not_null<Window::SessionController *> controller);

        [[nodiscard]] rpl::producer<QString> title() override;

    private:
        void SetupGeneral(not_null<Ui::VerticalLayout *> container);
        void SetupAppearance(not_null<Ui::VerticalLayout *> container);
        void SetupChats(not_null<Ui::VerticalLayout *> container);

        void SetupRabbitSettings(not_null<Ui::VerticalLayout *> container, not_null<Window::SessionController *> null);
        void setupContent(not_null<Window::SessionController *> controller);
    };

} // namespace Settings