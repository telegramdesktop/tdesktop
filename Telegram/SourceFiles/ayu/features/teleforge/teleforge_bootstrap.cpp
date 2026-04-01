#include "ayu/features/teleforge/teleforge_bootstrap.h"

#include "ayu/features/teleforge/teleforge_core.h"
#include "ayu/features/teleforge/teleforge_storage.h"

namespace TeleForge {

void initialize() {
	Storage::initialize();

	if (!LoadPersonalityCore().has_value()) {
		PersistPersonalityCore(DefaultPersonalityCore());
	}
}

} // namespace TeleForge
