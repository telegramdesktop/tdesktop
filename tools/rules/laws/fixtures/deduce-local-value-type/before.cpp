struct QBuffer {
	explicit QBuffer(int *) {}
};

void run(int &result) {
	QBuffer value(&result);
}
