#include "autodelete_box.h"

#include <crl/crl.h>
#include <base/qt/qt_key_modifiers.h>
#include <base/event_filter.h>
#include <api/api_common.h>

#include <ui/layers/box_content.h>
#include <ui/layers/generic_box.h>
#include <ui/widgets/input_fields.h>
#include <ui/widgets/buttons.h>

#include <chat_helpers/send_context_menu.h>

#include <styles/style_layers.h>
#include <styles/style_boxes.h>

#include <lang_auto.h>

using namespace Ui;

namespace {

struct ChooseTimeoutBoxDescriptor {
    QPointer<RoundButton> submit;
    Fn<TimeId()> collect;
};

struct ChooseTimeoutBoxArgs {
    rpl::producer<QString> title;
    TimeId timeout = 5;
    rpl::producer<QString> submit;
    Fn<void(TimeId)> done;
    ChooseDateTimeStyleArgs style;
};

class NumInput : public Ui::MaskedInputField {
public:
    NumInput(
        QWidget *parent,
        const style::InputField &st,
        const QString &value,
        int limit)
        : Ui::MaskedInputField(parent, st, nullptr, value)
        , _limit(limit) {}

protected:
    void correctValue(
            const QString &was,
            int wasCursor,
            QString &now,
            int &nowCursor) override {
        QString newText;
        newText.reserve(now.size());
        auto newPos = nowCursor;
        for (auto i = 0, l = int(now.size()); i < l; ++i) {
            if (now.at(i).isDigit()) {
                newText.append(now.at(i));
            } else if (i < nowCursor) {
                --newPos;
            }
        }
        bool ok;
        newText.toInt(&ok);
        if (!ok) {
            newText = QString();
            newPos = 0;
        } else if (_limit > 0 && newText.toInt() > _limit) {
            newText = was;
            newPos = wasCursor;
        }
        setCorrectedText(now, nowCursor, newText, newPos);
    }

private:
    int _limit;
};

}

static ChooseTimeoutBoxDescriptor ChooseTimeoutBox(
        not_null<GenericBox*> box,
        ChooseTimeoutBoxArgs &&args) {
    struct State {
        not_null<FlatLabel*> labelHours;
        not_null<NumInput*> hours;
        not_null<FlatLabel*> labelMinutes;
        not_null<NumInput*> minutes;
        not_null<FlatLabel*> labelSecond;
        not_null<NumInput*> seconds;
    };
    TimeId hours = args.timeout / 60 / 60,
        minutes = args.timeout / 60 % 60,
        seconds = args.timeout % 60;
    box->setTitle(std::move(args.title));
    box->setWidth(st::boxWidth);

    const auto content = box->addRow(object_ptr<FixedHeightWidget>(box, st::scheduleHeight));
    auto style = box->lifetime().make_state<style::InputField>(*args.style.dateFieldStyle);
    style->width = st::scheduleTimeWidth/2;
    const auto state = box->lifetime().make_state<State>(State{
        .labelHours = CreateChild<FlatLabel>(
            content,
            tr::lng_autodelete_hours(),
            *args.style.atStyle),
        .hours = CreateChild<NumInput>(
            content,
            *style,
            QString::number(hours).rightJustified(2, '0'),
            0),
        .labelMinutes = CreateChild<FlatLabel>(
            content,
            tr::lng_autodelete_minutes(),
            *args.style.atStyle),
        .minutes = CreateChild<NumInput>(
            content,
            *style,
            QString::number(minutes).rightJustified(2, '0'),
            59),
        .labelSecond = CreateChild<FlatLabel>(
            content,
            tr::lng_autodelete_seconds(),
            *args.style.atStyle),
        .seconds = CreateChild<NumInput>(
            content,
            *style,
            QString::number(seconds).rightJustified(2, '0'),
            59),
    });

    auto installScrollEvent = [](NumInput* input) {
        base::install_event_filter(input, [=](not_null<QEvent*> event) {
            if (event->type() == QEvent::Wheel) {
                const auto e = static_cast<QWheelEvent*>(event.get());
                const auto direction = Ui::WheelDirection(e);
                if (!direction) {
                    return base::EventFilterResult::Continue;
                }
                int current = input->text().toInt();
                int next = current + direction;
                input->setText(QString::number(next).rightJustified(2, '0'));
                input->onTextEdited();
                return base::EventFilterResult::Cancel;
            }
            return base::EventFilterResult::Continue;
        });
    };
    installScrollEvent(state->hours);
    installScrollEvent(state->minutes);
    installScrollEvent(state->seconds);

    content->widthValue()
        | rpl::start_with_next([=](int width) {
            const auto paddings = width
                - state->labelHours->width()
                - state->labelMinutes->width()
                - state->labelSecond->width()
                - state->hours->width()
                - state->minutes->width()
                - state->seconds->width();
            int left = paddings / 2;
            state->labelHours->moveToLeft(left, st::scheduleDateTop, width);
            left += state->labelHours->width();
            state->hours->moveToLeft(left, st::scheduleDateTop, width);
            left += state->hours->width();
            state->labelMinutes->moveToLeft(left, st::scheduleDateTop, width);
            left += state->labelMinutes->width();
            state->minutes->moveToLeft(left, st::scheduleDateTop, width);
            left += state->hours->width();
            state->labelSecond->moveToLeft(left, st::scheduleDateTop, width);
            left += state->labelSecond->width();
            state->seconds->moveToLeft(left, st::scheduleDateTop, width);
        }, content->lifetime());

    const auto collect = [=] {
        QString hours = state->hours->text(),
                minutes = state->minutes->text(),
                seconds = state->seconds->text();
        if (hours.isEmpty() && minutes.isEmpty() && seconds.isEmpty()) {
            return 0;
        }
        auto result = TimeId(
            hours.toInt(nullptr, 10) * 60 * 60
            + minutes.toInt(nullptr, 10) * 60
            + seconds.toInt(nullptr, 10));
        if (result == 0) {
             result = -1;
        }
        return result;
    };
    const auto save = [=, done = args.done] {
        if (const auto result = collect()) {
            done(result);
        } else {
            state->seconds->showError();
        }
    };
    /*state->time->submitRequests(
    ) | rpl::start_with_next(save, state->time->lifetime());*/

    auto result = ChooseTimeoutBoxDescriptor();
    box->setFocusCallback([=] { state->seconds->setFocusFast(); });
    result.submit = box->addButton(std::move(args.submit), save);
    result.collect = [=] {
        if (const auto result = collect()) {
            return result;
        }
        state->seconds->showError();
        return 0;
    };
    box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

    return result;
}

static void MakeAutoDeleteBox(
        not_null<GenericBox*> box,
        Fn<void(Api::SendOptions)> send,
        Ui::ChooseDateTimeStyleArgs style) {
    const auto save = [=](Api::SendOptions result) {
        if (!result.ptgAutoDelete) {
            return;
        }
        // Pro tip: Hold Ctrl key to send a silent scheduled message!
        result.silent = result.silent || base::IsCtrlPressed();
        const auto copy = send;
        box->closeBox();
        copy(result);
    };
    auto descriptor = ChooseTimeoutBox(box, {
        .title = tr::lng_autodelete_title(),
        .submit = tr::lng_send_button(),
        .done = [=](TimeId result) { save({ .ptgAutoDelete = result }); },
        .style = style,
    });

    SendMenu::SetupMenuAndShortcuts(
        descriptor.submit.data(),
        [=] { return SendMenu::Type::SilentOnly; },
        [=] { save({ .silent = true, .ptgAutoDelete = descriptor.collect() }); },
        nullptr,
        nullptr);
}

namespace FakePasscode {

object_ptr<Ui::GenericBox> AutoDeleteBox(
        not_null<Ui::RpWidget*> parent,
        Fn<void(Api::SendOptions)> send,
        Ui::ChooseDateTimeStyleArgs style) {
    auto callback = crl::guard(parent, send);
    return Box(MakeAutoDeleteBox, callback, style);
}

}
