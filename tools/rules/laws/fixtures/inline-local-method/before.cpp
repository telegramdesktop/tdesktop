namespace Editor {
class TextEditProxy {
public:
	void focusOutEvent() {
		sendUpdate();
	}

};
}
