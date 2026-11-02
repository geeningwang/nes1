#include "../common/nesdump.h"

// Standalone parser checker; exits before emulating anything.
int main(int argc, char** argv)
{
	nesdump::Config cfg;
	std::string err;
	if (!cfg.parse(argc, argv, err)) {
		fprintf(stderr, "%s\n", err.c_str());
		return 2;
	}
	printf("frames=%llu pads1=%08X command1=%02X fourscore=%d ports=%d,%d,%d\n",
		(unsigned long long)cfg.input.frames.size(), cfg.input_at(1), (unsigned)cfg.command_at(1),
		cfg.input.fourscore ? 1 : 0, cfg.input.ports[0], cfg.input.ports[1], cfg.input.ports[2]);
	return 0;
}
