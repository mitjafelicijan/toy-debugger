#include <lldb/API/LLDB.h>

#include <iostream>
#include <string>
#include <chrono>
#include <fstream>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <ctime>
#include <sstream>

#define TB_IMPL
#include "termbox2.h"

using namespace lldb;

const int LOG_WINDOW_HEIGHT = 10;
const int STATUS_WINDOW_HEIGHT = 1;
const int BREAKPOINTS_WINDOW_HEIGHT = 10;
const int SIDEBAR_WIDTH = 40;

struct LLDBGuard {
	LLDBGuard() { SBDebugger::Initialize(); }
	~LLDBGuard() { SBDebugger::Terminate(); }
};

struct TermboxGuard {
	TermboxGuard() { tb_init(); }
	~TermboxGuard() { tb_shutdown(); }
};

struct SourceCache {
	std::string path;
	std::vector<std::string> lines;

	const std::vector<std::string>& get_lines(const std::string& fullpath) {
		if (path != fullpath) {
			path = fullpath;
			lines.clear();
			std::ifstream file(fullpath);
			std::string line;
			while (std::getline(file, line)) {
				lines.push_back(line);
			}
		}
		return lines;
	}
};

enum AppMode {
	MODE_NORMAL,
	MODE_INPUT_BREAKPOINT,
	MODE_INPUT_VARIABLE
};

std::string get_timestamp() {
	auto now = std::chrono::system_clock::now();
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);
	std::tm now_tm = *std::localtime(&now_c);
	std::stringstream ss;
	ss << std::put_time(&now_tm, "%H:%M:%S");
	return ss.str();
}

void log_msg(std::vector<std::string>& log_buffer, const std::string& msg) {
	log_buffer.push_back(get_timestamp() + " " + msg);
}

void draw_text(int x, int y, uint16_t fg, uint16_t bg, const std::string& text) {
	for (char c : text) {
		tb_set_cell(x++, y, c, fg, bg);
	}
}

void draw_box(int x, int y, int w, int h, const std::string& title) {
	// Corners
	tb_set_cell(x, y, 0x250C, TB_DEFAULT, TB_DEFAULT);
	tb_set_cell(x + w - 1, y, 0x2510, TB_DEFAULT, TB_DEFAULT);
	tb_set_cell(x, y + h - 1, 0x2514, TB_DEFAULT, TB_DEFAULT);
	tb_set_cell(x + w - 1, y + h - 1, 0x2518, TB_DEFAULT, TB_DEFAULT);

	// Borders
	for (int i = 1; i < w - 1; ++i) {
		tb_set_cell(x + i, y, 0x2500, TB_DEFAULT, TB_DEFAULT);
		tb_set_cell(x + i, y + h - 1, 0x2500, TB_DEFAULT, TB_DEFAULT);
	}
	for (int i = 1; i < h - 1; ++i) {
		tb_set_cell(x, y + i, 0x2502, TB_DEFAULT, TB_DEFAULT);
		tb_set_cell(x + w - 1, y + i, 0x2502, TB_DEFAULT, TB_DEFAULT);
	}

	if (!title.empty()) {
		draw_text(x + 2, y, TB_BOLD | TB_GREEN, TB_DEFAULT, " " + title + " ");
	}
}

char get_type_char(SBType type) {
	if (!type.IsValid()) return '?';

	// Resolve typedefs to their underlying canonical type
	type = type.GetCanonicalType();

	if (type.IsPointerType()) return 'p';
	if (type.IsReferenceType()) return '&';
	if (type.IsArrayType()) return 'a';

	BasicType basic_type = type.GetBasicType();
	switch (basic_type) {
		case eBasicTypeInt:
		case eBasicTypeUnsignedInt:
			return 'i';
		case eBasicTypeChar:
		case eBasicTypeUnsignedChar:
			return 'c';
		case eBasicTypeFloat:
			return 'f';
		case eBasicTypeDouble:
			return 'd';
		case eBasicTypeBool:
			return 'b';
		case eBasicTypeLong:
		case eBasicTypeUnsignedLong:
		case eBasicTypeLongLong:
		case eBasicTypeUnsignedLongLong:
			return 'l';
		case eBasicTypeShort:
		case eBasicTypeUnsignedShort:
			return 's';
		case eBasicTypeVoid:
			return 'v';
		default:
			break;
	}

	TypeClass type_class = type.GetTypeClass();
	if (type_class & eTypeClassStruct) return 's';
	if (type_class & eTypeClassClass) return 'c';

	const char* name = type.GetName();
	if (name && *name) return name[0];

	return '?';
}

void draw_variable_recursive(SBValue val, int indent, int x, int start_y, int &current_offset, int max_height, int width) {
	if (current_offset >= max_height || indent > 3) return;

	std::string original_name = val.GetName() ? val.GetName() : "";
	char type_char = get_type_char(val.GetType());
	std::string prefix = std::string("(") + type_char + ") ";

	std::string value;
	if (!val.IsValid()) value = "(invalid)";
	else if (val.GetValue()) value = val.GetValue();
	else if (val.GetSummary()) value = val.GetSummary();

	std::string indent_str(indent * 2, ' ');
	std::string content = original_name;
	if (!value.empty()) content += " = " + value;

	std::string line = indent_str + prefix + content;
	if ((int)line.length() > width) line = line.substr(0, width - 3) + "...";

	int prefix_start = indent * 2;
	int prefix_end = prefix_start + 4; // length of "(x) "

	for (int i = 0; i < (int)line.length(); ++i) {
		uint16_t fg = TB_DEFAULT;
		if (i >= prefix_start && i < prefix_end) {
			fg = TB_BLACK | TB_BOLD;
		}
		tb_set_cell(x + i, start_y + current_offset, line[i], fg, TB_DEFAULT);
	}

	current_offset++;

	if (val.GetNumChildren() > 0) {
		uint32_t n = val.GetNumChildren();
		for (uint32_t i = 0; i < n; ++i) {
			draw_variable_recursive(val.GetChildAtIndex(i), indent + 1, x, start_y, current_offset, max_height, width);
		}
	}
}

void format_variable_log(SBValue val, std::vector<std::string>& log_buffer, int indent, const std::string& name_override = "") {
	if (indent > 3) return;

	std::string name = name_override.empty() ? (val.GetName() ? val.GetName() : "") : name_override;
	char type_char = get_type_char(val.GetType());

	std::string value;
	if (!val.IsValid()) value = "(invalid)";
	else if (val.GetValue()) value = val.GetValue();
	else if (val.GetSummary()) value = val.GetSummary();

	std::string indent_str(indent * 2, ' ');
	std::string line = get_timestamp() + " " + indent_str + "(" + type_char + ") " + name;
	if (!value.empty()) line += " = " + value;

	log_buffer.push_back(line);

	if (val.GetNumChildren() > 0) {
		uint32_t n = val.GetNumChildren();
		for (uint32_t i = 0; i < n; ++i) {
			format_variable_log(val.GetChildAtIndex(i), log_buffer, indent + 1);
		}
	}
}

void draw_variables_view(SBFrame &frame, int x, int y, int w, int h) {
	draw_box(x, y, w, h, "Locals");

	// Content area
	int cx = x + 1;
	int cy = y + 1;
	int ch = h - 2;
	int cw = w - 2;

	if (!frame.IsValid()) {
		draw_text(cx, cy, TB_RED, TB_DEFAULT, "No frame selected.");
		return;
	}

	SBValueList vars = frame.GetVariables(true, true, false, true);
	int current_offset = 0;

	for (uint32_t i = 0; i < vars.GetSize(); ++i) {
		draw_variable_recursive(vars.GetValueAtIndex(i), 0, cx, cy, current_offset, ch, cw);
	}
}

std::string get_breakpoint_name(SBBreakpoint bp) {
	if (!bp.IsValid()) return "???";

	std::string name = "???";
	if (bp.GetNumLocations() > 0) {
		SBBreakpointLocation loc = bp.GetLocationAtIndex(0);
		SBAddress addr = loc.GetAddress();

		SBFunction func = addr.GetFunction();
		if (func.IsValid()) {
			const char* n = func.GetName();
			if (n) name = n;
		} else {
			SBSymbol sym = addr.GetSymbol();
			if (sym.IsValid()) {
				const char* n = sym.GetName();
				if (n) name = n;
			}
		}

		SBLineEntry line_entry = addr.GetLineEntry();
		if (line_entry.IsValid()) {
			std::string file_name;
			SBFileSpec fs = line_entry.GetFileSpec();
			if (fs.IsValid()) file_name = fs.GetFilename();

			if (name == "???") {
				if (!file_name.empty()) name = file_name + ":" + std::to_string(line_entry.GetLine());
			} else {
				if (!file_name.empty()) name += " (" + file_name + ":" + std::to_string(line_entry.GetLine()) + ")";
			}
		}
	}
	return name;
}

void draw_source_view(SBFrame &frame, int x, int y, int w, int h, SourceCache& cache) {
	draw_box(x, y, w, h, "Source");

	int cx = x + 1;
	int cy = y + 1;
	int ch = h - 2;
	int cw = w - 2;

	if (!frame.IsValid()) {
		draw_text(cx, cy, TB_RED, TB_DEFAULT, "No frame selected.");
		return;
	}

	SBLineEntry line_entry = frame.GetLineEntry();
	if (!line_entry.IsValid()) {
		draw_text(cx, cy, TB_RED, TB_DEFAULT, "No line entry info.");
		return;
	}

	SBFileSpec file_spec = line_entry.GetFileSpec();
	if (!file_spec.IsValid()) return;

	// Construct full path
	std::string fullpath;
	if (file_spec.GetDirectory()) {
		fullpath = std::string(file_spec.GetDirectory()) + "/" + file_spec.GetFilename();
	} else {
		fullpath = file_spec.GetFilename();
	}

	SBAddress addr = frame.GetPCAddress();
	SBTarget target = frame.GetThread().GetProcess().GetTarget();

	const std::vector<std::string>& lines = cache.get_lines(fullpath);
	if (lines.empty() && !std::ifstream(fullpath).good()) {
		draw_text(cx, cy, TB_RED | TB_BOLD, TB_DEFAULT, "Could not open source: " + fullpath);

		SBFunction func = frame.GetFunction();
		std::string func_name = func.IsValid() ? func.GetName() : "???";

		char addr_buf[64];
		snprintf(addr_buf, sizeof(addr_buf), "At address: 0x%lx", (unsigned long)addr.GetLoadAddress(target));

		draw_text(cx, cy + 2, TB_WHITE, TB_DEFAULT, "Function: " + func_name);
		draw_text(cx, cy + 3, TB_WHITE, TB_DEFAULT, addr_buf);
		draw_text(cx, cy + 5, TB_YELLOW, TB_DEFAULT, "Press 'n' (Step Over) or 'o' (Step Out) to return to your code.");

		// Disassembly fallback
		SBInstructionList instructions = target.ReadInstructions(addr, (uint32_t)(ch - 8));
		if (instructions.IsValid()) {
			for (uint32_t i = 0; i < instructions.GetSize() && (int)i < ch - 8; ++i) {
				SBInstruction insn = instructions.GetInstructionAtIndex(i);
				std::string dis = insn.GetMnemonic(target);
				dis += " ";
				dis += insn.GetOperands(target);

				uint16_t fg = (insn.GetAddress() == addr) ? TB_WHITE | TB_BOLD : TB_DEFAULT;
				uint16_t bg = (insn.GetAddress() == addr) ? TB_BLUE : TB_DEFAULT;

				char insn_addr_buf[32];
				snprintf(insn_addr_buf, sizeof(insn_addr_buf), "0x%lx: ", (unsigned long)insn.GetAddress().GetLoadAddress(target));

				draw_text(cx, cy + 7 + i, fg, bg, std::string(insn_addr_buf) + dis);
			}
		}
		return;
	}

	int current_line = line_entry.GetLine();
	int half_height = ch / 2;
	int start_line = std::max(1, current_line - half_height);
	int end_line = std::min((int)lines.size(), start_line + ch - 1);
	if (end_line - start_line + 1 < ch) {
		start_line = std::max(1, end_line - ch + 1);
	}

	for (int i = 0; i < ch; ++i) {
		int line_idx = start_line + i;
		if (line_idx > end_line) break;

		std::string src = lines[line_idx - 1];
		// Handle basic tab expansion (simple version)
		std::string expanded;
		for (char c : src) {
			if (c == '\t') expanded += "    ";
			else expanded += c;
		}
		src = expanded;

		bool is_current = (line_idx == current_line);

		char buf[32];
		snprintf(buf, sizeof(buf), "%4d ", line_idx);
		std::string num_str(buf);

		uint16_t bg = is_current ? TB_BLUE : TB_DEFAULT;
		uint16_t fg = is_current ? TB_WHITE | TB_BOLD : TB_DEFAULT;

		draw_text(cx, cy + i, fg, bg, num_str);

		int src_max_len = cw - (int)num_str.length();
		if ((int)src.length() > src_max_len) {
			src = src.substr(0, src_max_len);
		}
		draw_text(cx + num_str.length(), cy + i, fg, bg, src);

		if (is_current) {
			for (int k = cx + num_str.length() + src.length(); k < cx + cw; ++k) {
				tb_set_cell(k, cy + i, ' ', fg, bg);
			}
		}
	}
}

void draw_breakpoints_view(SBTarget& target, int x, int y, int w, int h) {
	draw_box(x, y, w, h, "Breakpoints");
	int cx = x + 1;
	int cy = y + 1;
	int mh = h - 2;

	if (!target.IsValid()) return;

	int num_bps = target.GetNumBreakpoints();
	for (int i = 0; i < num_bps && i < mh; ++i) {
		SBBreakpoint bp = target.GetBreakpointAtIndex(i);
		std::string name = get_breakpoint_name(bp);

		char buf[128];
		snprintf(buf, sizeof(buf), "%d: %s", bp.GetID(), name.c_str());
		std::string line = buf;
		draw_text(cx, cy + i, TB_DEFAULT, TB_DEFAULT, line);
	}
}

SBBreakpoint create_breakpoint(SBTarget& target, const std::string& input) {
	SBBreakpoint bp;
	size_t colon_pos = input.rfind(':');

	if (colon_pos != std::string::npos && colon_pos < input.length() - 1) {
		std::string line_str = input.substr(colon_pos + 1);
		bool is_number = !line_str.empty() && std::all_of(line_str.begin(), line_str.end(), ::isdigit);

		if (is_number) {
			std::string filename = input.substr(0, colon_pos);
			uint32_t line_no = (uint32_t)std::stoi(line_str);
			bp = target.BreakpointCreateByLocation(filename.c_str(), line_no);
			if (bp.IsValid() && bp.GetNumLocations() > 0) return bp;
		}
	}

	return target.BreakpointCreateByName(input.c_str());
}

void draw_log_view(int x, int y, int w, int h, const std::vector<std::string>& log_buffer, AppMode mode, const std::string& input_buffer) {
	bool input_mode = (mode == MODE_INPUT_BREAKPOINT || mode == MODE_INPUT_VARIABLE);
	std::string title = input_mode ? "Input (Esc to Cancel)" : "Command & Log";
	draw_box(x, y, w, h, title);

	int cx = x + 1;
	int cy = y + 1;
	int ch = h - 2;
	int cw = w - 2;

	if (input_mode) {
		std::string prompt;
		if (mode == MODE_INPUT_BREAKPOINT) prompt = "Add Breakpoint: ";
		else if (mode == MODE_INPUT_VARIABLE) prompt = "Print Variable: ";

		prompt += input_buffer;
		if ((int)prompt.length() > cw) prompt = prompt.substr(prompt.length() - cw);
		draw_text(cx, cy, TB_WHITE | TB_BOLD, TB_DEFAULT, prompt);
		tb_set_cell(cx + prompt.length(), cy, '_', TB_WHITE | TB_BOLD | TB_REVERSE, TB_DEFAULT);
	} else {
		int log_lines_count = std::min((int)log_buffer.size(), ch);
		for (int i = 0; i < log_lines_count; ++i) {
			const std::string& msg = log_buffer[log_buffer.size() - log_lines_count + i];
			std::string disp = msg;
			if ((int)disp.length() > cw) disp = disp.substr(0, cw);
			draw_text(cx, cy + i, TB_DEFAULT, TB_DEFAULT, disp);
		}
	}
}

void draw_status_bar(SBProcess &process, AppMode mode, int width, int height) {
	std::string state_str = "Status: ";
	if (!process.IsValid()) {
		state_str += "Not Running";
	} else {
		StateType state = process.GetState();
		if (state == eStateStopped) state_str += "Stopped";
		else if (state == eStateRunning) state_str += "Running";
		else if (state == eStateExited) state_str += "Exited";
		else state_str += "Unknown";
	}

	state_str += (mode == MODE_NORMAL)
		? " | r=Run, b=Add breakpoint, p=Print, n=Step Over, s=Step Into, o=Step Out, c=Continue, q=Quit"
		: " | Enter=Confirm, Esc=Cancel";

	for (int x = 0; x < width; ++x) {
		tb_set_cell(x, height - 1, ' ', TB_BLACK, TB_WHITE);
	}

	draw_text(1, height - 1, TB_BLACK, TB_WHITE, state_str);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <target_executable>\n";
		return 1;
	}

	int log_fd = open("tdbg.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (log_fd != -1) {
		dup2(log_fd, STDERR_FILENO);
		close(log_fd);
	}

	const char* target_path = argv[1];

	LLDBGuard lldb_guard;
	SBDebugger debugger = SBDebugger::Create();
	debugger.SetAsync(false); 

	SBTarget target = debugger.CreateTarget(target_path);
	if (!target.IsValid()) {
		std::cerr << "Failed to create target for " << target_path << "\n";
		return 1;
	}

	SBProcess process; 
	SBThread thread;

	TermboxGuard tb_guard;

	bool running = true;
	AppMode mode = MODE_NORMAL;
	std::string input_buffer;
	std::vector<std::string> log_buffer;
	SourceCache source_cache;
	log_buffer.push_back("Debugger started. Press 'b' to add breakpoint, 'r' to run.");

	while (running) {
		tb_clear();

		int width = tb_width();
		int height = tb_height();
		int main_window_height = height - LOG_WINDOW_HEIGHT - STATUS_WINDOW_HEIGHT;
		int split_x = width - SIDEBAR_WIDTH;
		int locals_window_height = main_window_height - BREAKPOINTS_WINDOW_HEIGHT;

		SBFrame frame;
		if (process.IsValid() && process.GetState() != eStateExited) {
			thread = process.GetSelectedThread();
			if (thread.IsValid()) {
				frame = thread.GetSelectedFrame();
			}
		}

		draw_source_view(frame, 0, 0, split_x, main_window_height, source_cache);
		draw_variables_view(frame, split_x, 0, SIDEBAR_WIDTH, locals_window_height);
		draw_breakpoints_view(target, split_x, locals_window_height, SIDEBAR_WIDTH, BREAKPOINTS_WINDOW_HEIGHT);
		draw_log_view(0, main_window_height, width, LOG_WINDOW_HEIGHT, log_buffer, mode, input_buffer);
		draw_status_bar(process, mode, width, height);

		tb_present();

		struct tb_event ev;
		if (tb_poll_event(&ev) == 0) {
			if (ev.type == TB_EVENT_KEY) {
				if (mode == MODE_NORMAL) {
					if (ev.ch == 'q') {
						running = false;
					} else if (ev.ch == 'r') {
						if (!process.IsValid()) {
							if (target.GetNumBreakpoints() == 0) {
								SBBreakpoint bp = target.BreakpointCreateByName("main");
								if (bp.IsValid() && bp.GetNumLocations() > 0) {
									log_msg(log_buffer, "No breakpoints. Added breakpoint at 'main'");
								} else {
									log_msg(log_buffer, "No breakpoints. Failed to add breakpoint at 'main'");
								}
							}
							log_msg(log_buffer, "Launching...");
							process = target.LaunchSimple(nullptr, nullptr, ".");
							if (!process.IsValid()) log_msg(log_buffer, "Launch failed");
							else log_msg(log_buffer, "Launched");
						} else {
							log_msg(log_buffer, "Already running");
						}
					} else if (ev.ch == 'b') {
						mode = MODE_INPUT_BREAKPOINT;
						input_buffer.clear();
					} else if (ev.ch == 'p') {
						mode = MODE_INPUT_VARIABLE;
						input_buffer.clear();
					} else {
						if (process.IsValid() && process.GetState() == eStateStopped) {
							switch (ev.ch) {
								case 'n': if (thread.IsValid()) thread.StepOver(); break;
								case 's': if (thread.IsValid()) thread.StepInto(); break;
								case 'o': if (thread.IsValid()) thread.StepOut(); break;
								case 'c': process.Continue(); break;
							}
						}
					}
				} else if (mode == MODE_INPUT_BREAKPOINT || mode == MODE_INPUT_VARIABLE) {
					if (ev.key == TB_KEY_ESC) {
						mode = MODE_NORMAL;
						input_buffer.clear();
					} else if (ev.key == TB_KEY_ENTER) {
						if (!input_buffer.empty()) {
							if (mode == MODE_INPUT_BREAKPOINT) {
								SBBreakpoint bp = create_breakpoint(target, input_buffer);
								if (bp.IsValid() && bp.GetNumLocations() > 0) {
									log_msg(log_buffer, "Breakpoint added: " + input_buffer);
								} else {
									log_msg(log_buffer, "Failed/Invalid breakpoint: " + input_buffer);
								}
							} else if (mode == MODE_INPUT_VARIABLE) {
								if (!frame.IsValid()) {
									log_msg(log_buffer, "Error: No stack frame available to evaluate '" + input_buffer + "'");
								} else {
									SBValue val = frame.EvaluateExpression(input_buffer.c_str());
									if (val.IsValid() && !val.GetError().Fail()) {
										format_variable_log(val, log_buffer, 0, input_buffer);
									} else {
										std::string err = "Error evaluating '" + input_buffer + "'";
										if (val.GetError().GetCString()) {
											err += ": ";
											err += val.GetError().GetCString();
										}
										log_msg(log_buffer, err);
									}
								}
							}
						}
						mode = MODE_NORMAL;
						input_buffer.clear();
					} else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
						if (!input_buffer.empty()) input_buffer.pop_back();
					} else if (ev.ch != 0) {
						input_buffer += (char)ev.ch;
					}
				}
			}
		}
	}

	return 0;
}
