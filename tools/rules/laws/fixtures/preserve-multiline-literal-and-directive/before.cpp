void paint(const char *value);
#define FINISH(x) x \
	+ 1
void run() {
	paint(
		R"(stay
	  as-is)"
	);
}
