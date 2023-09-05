/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/flags.h"

template <typename Flags>
struct EditFlagsDescriptor;

namespace PowerSaving {
enum Flag : uint32;
using Flags = base::flags<Flag>;
} // namespace PowerSaving

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Settings {

void PowerSavingBox(not_null<Ui::GenericBox*> box);

[[nodiscard]] EditFlagsDescriptor<PowerSaving::Flags> PowerSavingLabels();

} // namespace PowerSaving
