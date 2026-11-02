// Deterministic controller replay shared by the headless runners.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <climits>

namespace nesdump {

struct InputFrame {
	uint32_t pads = 0;
	uint8_t commands = 0;
};

struct InputFile {
	bool versioned = false;
	bool fourscore = false;
	int ports[3] = { 1, 1, 0 };
	std::vector<InputFrame> frames;
	uint64_t rom_hash = 0;
	uint32_t rom_size = 0;

	static bool number(const std::string& text, unsigned base, uint64_t max, uint64_t& value)
	{
		if (text.empty()) return false;
		value = 0;
		for (size_t i = 0; i < text.size(); ++i) {
			const char c = text[i];
			unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
				c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) :
				c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : base;
			if (digit >= base || digit > max || value > (max - digit) / base) return false;
			value = value * base + digit;
		}
		return true;
	}

	bool load(const std::string& path, std::string& err)
	{
		*this = InputFile();
		FILE* file = nullptr;
#ifdef _MSC_VER
		if (fopen_s(&file, path.c_str(), "rb") != 0) file = nullptr;
#else
		file = fopen(path.c_str(), "rb");
#endif
		if (!file) { err = "cannot open input file " + path; return false; }
		char line[512];
		unsigned lineno = 0, fields = 0;
		bool first = true, data = false, ok = true;
		for (;;) {
			size_t length = 0;
			bool invalid_line = false;
			int ch = EOF;
			while ((ch = fgetc(file)) != EOF) {
				if (length + 1 == sizeof(line)) { invalid_line = true; break; }
				if (ch == 0) invalid_line = true;
				line[length++] = (char)ch;
				if (ch == '\n') break;
			}
			if (length == 0 && ch == EOF) break;
			line[length] = 0;
			++lineno;
			std::string problem;
			if (invalid_line) problem = "line too long or contains a NUL byte";
			std::istringstream stream(line);
			std::vector<std::string> tokens;
			std::string token;
			while (stream >> token) tokens.push_back(token);
			if (tokens.empty() || tokens[0][0] == '#') {
				if (problem.empty()) continue;
			}
			if (problem.empty() && first) {
				first = false;
				if (tokens[0] == "NESINPUT") {
					if (tokens.size() != 2 || tokens[1] != "1") problem = "unsupported NESINPUT version";
					else { versioned = true; continue; }
				}
			}
			if (problem.empty() && versioned && !data) {
				if (tokens.size() == 1 && tokens[0] == "data") {
					if (fields != 127) problem = "missing required replay metadata";
					else { data = true; continue; }
				} else if (tokens.size() != 1) {
					problem = "expected key=value metadata or data";
				} else {
					const size_t eq = tokens[0].find('=');
					const std::string key = tokens[0].substr(0, eq);
					const std::string val = eq == std::string::npos ? "" : tokens[0].substr(eq + 1);
					uint64_t value = 0;
					unsigned bit = 0;
					if (key == "rom_hash") {
						bit = 1;
						if (val.size() != 16 || !number(val, 16, UINT64_MAX, value)) problem = "invalid rom_hash";
						else rom_hash = value;
					} else if (key == "rom_size") {
						bit = 2;
						if (!number(val, 10, UINT32_MAX, value) || value < 16) problem = "invalid rom_size";
						else rom_size = (uint32_t)value;
					} else if (key == "fourscore") {
						bit = 4;
						if (!number(val, 10, 1, value)) problem = "invalid fourscore";
						else fourscore = value != 0;
					} else if (key == "ram_init") {
						bit = 8;
						if (val != "0") problem = "only deterministic ram_init=0 is supported";
					} else if (key == "port0" || key == "port1" || key == "port2") {
						const int p = key[4] - '0';
						bit = 16u << p;
						if (!number(val, 10, 3, value) || (p < 2 && value > 1) || (p == 2 && value != 0 && value != 3))
							problem = "unsupported input port";
						else ports[p] = (int)value;
					} else problem = "unknown replay metadata";
					if (bit && (fields & bit)) problem = "duplicate replay metadata";
					fields |= bit;
					if (problem.empty()) continue;
				}
			} else if (problem.empty()) {
				const size_t expected = versioned ? 5 : tokens.size();
				if ((versioned && tokens.size() != expected) || (!versioned && (tokens.empty() || tokens.size() > 2))) {
					problem = "invalid frame field count";
				} else {
					InputFrame frame;
					for (size_t i = 0; i < tokens.size(); ++i) {
						uint64_t value = 0;
						if (tokens[i].size() > 2 || !number(tokens[i], 16, 255, value)) {
							problem = "invalid hex byte";
							break;
						}
						if (versioned && i == 0) {
							if (value & ~3u) { problem = "unsupported movie command"; break; }
							frame.commands = (uint8_t)value;
						} else frame.pads |= (uint32_t)value << ((i - (versioned ? 1 : 0)) * 8);
					}
					if (problem.empty()) frames.push_back(frame);
				}
			}
			if (!problem.empty()) {
				err = path + ":" + std::to_string(lineno) + ": " + problem;
				ok = false;
				break;
			}
		}
		if (ok && ferror(file)) { err = "error reading input file " + path; ok = false; }
		fclose(file);
		if (!ok) return false;
		if (frames.empty() || (versioned && !data)) { err = path + ": no replay frames"; return false; }
		if (fourscore && (ports[0] != 1 || ports[1] != 1)) {
			err = path + ": Four Score requires both standard ports"; return false;
		}
		return true;
	}

	bool validate_rom(const std::string& path, std::string& err) const
	{
		if (!versioned) return true;
		FILE* file = nullptr;
#ifdef _MSC_VER
		if (fopen_s(&file, path.c_str(), "rb") != 0) file = nullptr;
#else
		file = fopen(path.c_str(), "rb");
#endif
		if (!file) { err = "cannot open replay ROM " + path; return false; }
		uint64_t hash = UINT64_C(14695981039346656037), size = 0;
		unsigned char bytes[4096];
		size_t n;
		while ((n = fread(bytes, 1, sizeof(bytes), file)) != 0) {
			size += n;
			for (size_t i = 0; i < n; ++i) { hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
		}
		const bool io_error = ferror(file) != 0;
		fclose(file);
		if (io_error) { err = "error reading replay ROM " + path; return false; }
		if (size != rom_size || hash != rom_hash) { err = "replay ROM identity mismatch: " + path; return false; }
		return true;
	}

	bool validate_run(int count, std::string& err) const
	{
		if (versioned && (size_t)count > frames.size()) {
			err = "requested run exceeds replay length"; return false;
		}
		return true;
	}

	InputFrame at(int frame) const
	{
		return frame >= 1 && (size_t)frame <= frames.size() ? frames[(size_t)frame - 1] : InputFrame();
	}
};

} // namespace nesdump
