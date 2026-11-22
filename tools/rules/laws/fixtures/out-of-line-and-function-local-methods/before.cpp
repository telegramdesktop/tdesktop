namespace Editor {
class TextEditProxy {
public:
	void focusOutEvent();

};
void TextEditProxy::focusOutEvent() {
	sendUpdate();
}
void run() {
	class Local {
	public:
		void update() {
			refresh();
		}
	};
}
}
