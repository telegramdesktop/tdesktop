import dataclasses
import importlib.util
import json
import pathlib
import sys
import threading
import time
import traceback


ROOT = pathlib.Path(__file__).resolve().parent
PLUGINS = ROOT / "plugins"


@dataclasses.dataclass
class Sender:
    id: int = 0
    is_blocked: bool = False


@dataclasses.dataclass
class Chat:
    id: int = 0


@dataclasses.dataclass
class Message:
    id: int = 0
    text: str = ""
    sender: Sender = dataclasses.field(default_factory=Sender)
    chat: Chat = dataclasses.field(default_factory=Chat)
    date: int = 0
    is_service: bool = False


class Api:
    def _command(self, command, **payload):
        data = {"type": "command", "command": command}
        data.update(payload)
        print(json.dumps(data, ensure_ascii=False), flush=True)

    def set_rule(self, name, value):
        self._command("set_rule", name=name, value=bool(value))

    def hide_user(self, user_id):
        self._command("hide_user", user_id=int(user_id))

    def show_user(self, user_id):
        self._command("show_user", user_id=int(user_id))

    def clear_hidden_users(self):
        self._command("clear_hidden_users")

    def log(self, message):
        self._command("log", message=str(message))


class Plugin:
    def __init__(self, path):
        self.path = path
        self.name = path.stem
        self.mtime = 0.0
        self.module = None


api = Api()
plugins = {}
plugins_lock = threading.RLock()


def log(message):
    api.log(message)


def traceback_to_log(prefix):
    log(prefix + "\n" + traceback.format_exc())


def module_from_path(path):
    name = "tdesktop_plugin_" + path.stem
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load plugin spec")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def unload(plugin):
    if plugin.module and hasattr(plugin.module, "on_unload"):
        try:
            plugin.module.on_unload(api)
        except Exception:
            traceback_to_log("[PLUGIN] unload failed " + plugin.name)
    plugin.module = None


def load(plugin, reloaded=False):
    try:
        plugin.module = module_from_path(plugin.path)
        plugin.mtime = plugin.path.stat().st_mtime
        if hasattr(plugin.module, "on_load"):
            plugin.module.on_load(api)
        log("[PLUGIN] " + ("reload " if reloaded else "loaded: ") + plugin.name)
    except Exception:
        plugin.module = None
        plugin.mtime = plugin.path.stat().st_mtime if plugin.path.exists() else 0.0
        traceback_to_log("[PLUGIN] load failed " + plugin.name)


def scan_plugins():
    PLUGINS.mkdir(parents=True, exist_ok=True)
    seen = set()
    with plugins_lock:
        for path in sorted(PLUGINS.glob("*.py")):
            if path.name.startswith("_"):
                continue
            seen.add(path)
            plugin = plugins.get(path)
            mtime = path.stat().st_mtime
            if plugin is None:
                plugin = Plugin(path)
                plugins[path] = plugin
                load(plugin)
            elif mtime != plugin.mtime:
                unload(plugin)
                load(plugin, reloaded=True)
        for path in list(plugins):
            if path not in seen:
                unload(plugins.pop(path))


def watcher():
    while True:
        try:
            scan_plugins()
        except Exception:
            traceback_to_log("[PLUGIN] scan failed")
        time.sleep(0.5)


def make_message(data):
    sender_data = data.get("sender") or {}
    chat_data = data.get("chat") or {}
    return Message(
        id=int(data.get("id") or data.get("message_id") or 0),
        text=str(data.get("text") or ""),
        sender=Sender(
            id=int(sender_data.get("id") or data.get("sender_id") or 0),
            is_blocked=bool(sender_data.get("is_blocked") or data.get("sender_is_blocked")),
        ),
        chat=Chat(id=int(chat_data.get("id") or data.get("chat_id") or 0)),
        date=int(data.get("date") or 0),
        is_service=bool(data.get("is_service")),
    )


def dispatch_message(data):
    message = make_message(data)
    with plugins_lock:
        current = list(plugins.values())
    for plugin in current:
        module = plugin.module
        if module and hasattr(module, "on_message"):
            try:
                module.on_message(message)
            except Exception:
                traceback_to_log("[PLUGIN] on_message failed " + plugin.name)


def main():
    scan_plugins()
    threading.Thread(target=watcher, daemon=True).start()
    for line in sys.stdin:
        try:
            payload = json.loads(line)
            if payload.get("type") == "event" and payload.get("event") == "message":
                dispatch_message(payload.get("data") or {})
        except Exception:
            traceback_to_log("[PLUGIN] event failed")


if __name__ == "__main__":
    main()
