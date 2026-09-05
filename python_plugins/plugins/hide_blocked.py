def on_load(api):
    api.log("Hide Blocked Users loaded")
    api.set_rule("hide_blocked", True)


def on_unload(api):
    api.set_rule("hide_blocked", False)
