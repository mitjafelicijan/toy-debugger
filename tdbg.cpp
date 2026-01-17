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

struct LayoutConfig {
	int log_height = 10;
	int status_height = 1;
	int breakpoints_height = 10;
	int sidebar_width = 50;
} layout_config;

// https://unicodeplus.com
const uint32_t SCROLLBAR_THUMB = 0x2593; // Dark shade
const uint32_t SCROLLBAR_LINE = 0x2502;  // Vertical line
const uint32_t BREAKPOINT_CIRCLE = 0x25B6; // Filled triangle

enum InputMode {
	INPUT_MODE_NORMAL,
	INPUT_MODE_BREAKPOINT,
	INPUT_MODE_VARIABLE
};

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

struct VarLine {
	std::string text;
	int indent;
	int prefix_start;
	int prefix_end;
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
	if (type_class & eTypeClassEnumeration) return 'e';

	const char* name = type.GetName();
	if (name && *name) return name[0];

	return '?';
}

void collect_variables_recursive(SBValue val, int indent, std::vector<VarLine>& lines, int width) {
	if (indent > 3) return;

	std::string original_name = val.GetName() ? val.GetName() : "";
	char type_char = get_type_char(val.GetType());
	std::string prefix = std::string("(") + type_char + ") ";

	std::string val_str = val.GetValue() ? val.GetValue() : "";
	std::string summary_str = val.GetSummary() ? val.GetSummary() : "";
	std::string value;

	if (!val.IsValid()) value = "(invalid)";
	else {
		if (!val_str.empty() && !summary_str.empty()) value = val_str + " " + summary_str;
		else if (!val_str.empty()) value = val_str;
		else if (!summary_str.empty()) value = summary_str;
	}

	std::string indent_str(indent * 2, ' ');
	std::string content = original_name;
	if (!value.empty()) content += " = " + value;

	std::string line_text = indent_str + prefix + content;
	if ((int)line_text.length() > width) line_text = line_text.substr(0, width - 3) + "...";

	VarLine vl;
	vl.text = line_text;
	vl.indent = indent;
	vl.prefix_start = indent * 2;
	vl.prefix_end = vl.prefix_start + 4; // length of "(x) "
	lines.push_back(vl);

	if (val.GetNumChildren() > 0) {
		uint32_t n = val.GetNumChildren();
		for (uint32_t i = 0; i < n; ++i) {
			collect_variables_recursive(val.GetChildAtIndex(i), indent + 1, lines, width);
		}
	}
}

void format_variable_log(SBValue val, std::vector<std::string>& log_buffer, int indent, const std::string& name_override = "") {
	if (indent > 3) return;

	std::string name = name_override.empty() ? (val.GetName() ? val.GetName() : "") : name_override;
	char type_char = get_type_char(val.GetType());

	std::string val_str = val.GetValue() ? val.GetValue() : "";
	std::string summary_str = val.GetSummary() ? val.GetSummary() : "";
	std::string value;

	if (!val.IsValid()) value = "(invalid)";
	else {
		if (!val_str.empty() && !summary_str.empty()) value = val_str + " " + summary_str;
		else if (!val_str.empty()) value = val_str;
		else if (!summary_str.empty()) value = summary_str;
	}

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

void draw_variables_view(SBFrame &frame, int x, int y, int w, int h, int scroll_offset) {
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

	std::vector<VarLine> lines;
	SBValueList vars = frame.GetVariables(true, true, false, true);
	for (uint32_t i = 0; i < vars.GetSize(); ++i) {
		collect_variables_recursive(vars.GetValueAtIndex(i), 0, lines, cw);
	}

	int total_lines = (int)lines.size();
	int display_count = std::min(total_lines, ch);

	for (int i = 0; i < display_count; ++i) {
		int line_idx = scroll_offset + i;
		if (line_idx < 0 || line_idx >= total_lines) continue;

		const VarLine& vl = lines[line_idx];
		for (int j = 0; j < (int)vl.text.length() && j < cw; ++j) {
			uint16_t fg = TB_DEFAULT;
			if (j >= vl.prefix_start && j < vl.prefix_end) {
				fg = TB_BLACK | TB_BOLD;
			}
			tb_set_cell(cx + j, cy + i, vl.text[j], fg, TB_DEFAULT);
		}
	}

	// Draw scrollbar
	if (total_lines > ch) {
		int thumb_height = std::max(1, (ch * ch) / total_lines);
		int max_scroll = total_lines - ch;
		double scroll_percent = (double)scroll_offset / (double)max_scroll;
		int thumb_pos = (ch - thumb_height) * scroll_percent;

		for (int i = 0; i < ch; ++i) {
			uint32_t cell_char = SCROLLBAR_LINE;
			uint16_t fg = TB_DEFAULT;
			if (i >= thumb_pos && i < thumb_pos + thumb_height) {
				cell_char = SCROLLBAR_THUMB;
				fg = TB_WHITE;
			}
			tb_set_cell(x + w - 1, cy + i, cell_char, fg, TB_DEFAULT);
		}
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

void draw_source_view(SBFrame &frame, int x, int y, int w, int h, SourceCache& cache, int scroll_offset) {
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

	// Get breakpoints for this file
	std::vector<uint32_t> bp_lines;
	uint32_t num_breakpoints = target.GetNumBreakpoints();
	for (uint32_t i = 0; i < num_breakpoints; ++i) {
		SBBreakpoint bp = target.GetBreakpointAtIndex(i);
		uint32_t num_locs = bp.GetNumLocations();
		for (uint32_t j = 0; j < num_locs; ++j) {
			SBBreakpointLocation loc = bp.GetLocationAtIndex(j);
			SBLineEntry le = loc.GetAddress().GetLineEntry();
			if (le.IsValid()) {
				SBFileSpec fs = le.GetFileSpec();
				if (fs.IsValid()) {
					std::string bp_path;
					if (fs.GetDirectory()) {
						bp_path = std::string(fs.GetDirectory()) + "/" + fs.GetFilename();
					} else {
						bp_path = fs.GetFilename();
					}
					if (bp_path == fullpath) {
						bp_lines.push_back(le.GetLine());
					}
				}
			}
		}
	}

	int total_lines = (int)lines.size();
	int current_line = line_entry.GetLine();
	for (int i = 0; i < ch; ++i) {
		int line_idx = scroll_offset + i + 1;
		if (line_idx > total_lines) break;

		std::string src = lines[line_idx - 1];
		// Handle basic tab expansion (simple version)
		std::string expanded;
		for (char c : src) {
			if (c == '\t') expanded += "    ";
			else expanded += c;
		}
		src = expanded;

		bool is_current = (line_idx == current_line);
		bool has_breakpoint = std::find(bp_lines.begin(), bp_lines.end(), (uint32_t)line_idx) != bp_lines.end();

		char buf[32];
		snprintf(buf, sizeof(buf), "%4d ", line_idx);
		std::string num_str(buf);

		uint16_t bg = is_current ? TB_BLUE : TB_DEFAULT;
		uint16_t fg = is_current ? TB_WHITE | TB_BOLD : TB_DEFAULT;

		// Draw breakpoint indicator
		if (has_breakpoint) {
			tb_set_cell(cx, cy + i, BREAKPOINT_CIRCLE, TB_RED | TB_BOLD, bg);
		} else {
			tb_set_cell(cx, cy + i, ' ', fg, bg);
		}

		draw_text(cx + 1, cy + i, fg, bg, num_str);

		int src_max_len = cw - (int)num_str.length() - 1;
		if ((int)src.length() > src_max_len) {
			src = src.substr(0, src_max_len);
		}
		draw_text(cx + 1 + num_str.length(), cy + i, fg, bg, src);

		if (is_current) {
			for (int k = cx + 1 + num_str.length() + src.length(); k < cx + cw; ++k) {
				tb_set_cell(k, cy + i, ' ', fg, bg);
			}
		}
	}

	// Draw scrollbar
	if (total_lines > ch) {
		int thumb_height = std::max(1, (ch * ch) / total_lines);
		int max_scroll = total_lines - ch;
		double scroll_percent = (double)scroll_offset / (double)max_scroll;
		int thumb_pos = (ch - thumb_height) * scroll_percent;

		for (int i = 0; i < ch; ++i) {
			uint32_t cell_char = SCROLLBAR_LINE;
			uint16_t fg = TB_DEFAULT;
			if (i >= thumb_pos && i < thumb_pos + thumb_height) {
				cell_char = SCROLLBAR_THUMB;
				fg = TB_WHITE;
			}
			tb_set_cell(x + w - 1, cy + i, cell_char, fg, TB_DEFAULT);
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

void draw_log_view(int x, int y, int w, int h, const std::vector<std::string>& log_buffer, InputMode mode, const std::string& input_buffer, int scroll_offset) {
	bool input_mode = (mode == INPUT_MODE_BREAKPOINT || mode == INPUT_MODE_VARIABLE);
	std::string title = input_mode ? "Input (Esc to Cancel)" : "Logs";
	if (!input_mode && scroll_offset > 0) {
		title += " (Scrolled up: " + std::to_string(scroll_offset) + ")";
	}
	draw_box(x, y, w, h, title);

	int cx = x + 1;
	int cy = y + 1;
	int ch = h - 2;
	int cw = w - 2;

	if (input_mode) {
		std::string prompt;
		if (mode == INPUT_MODE_BREAKPOINT) prompt = "Add Breakpoint: ";
		else if (mode == INPUT_MODE_VARIABLE) prompt = "Print Variable: ";

		prompt += input_buffer;
		if ((int)prompt.length() > cw) prompt = prompt.substr(prompt.length() - cw);
		draw_text(cx, cy, TB_WHITE | TB_BOLD, TB_DEFAULT, prompt);
		tb_set_cell(cx + prompt.length(), cy, '_', TB_WHITE | TB_BOLD | TB_REVERSE, TB_DEFAULT);
	} else {
		int total_logs = log_buffer.size();
		int display_count = std::min(total_logs, ch);

		for (int i = 0; i < display_count; ++i) {
			int log_idx = total_logs - display_count - scroll_offset + i;
			if (log_idx < 0 || log_idx >= total_logs) continue;

			const std::string& msg = log_buffer[log_idx];
			std::string disp = msg;
			if ((int)disp.length() > cw) disp = disp.substr(0, cw);
			draw_text(cx, cy + i, TB_DEFAULT, TB_DEFAULT, disp);
		}

		// Draw scrollbar
		if (total_logs > ch) {
			int thumb_height = std::max(1, (ch * ch) / total_logs);
			int max_scroll = total_logs - ch;
			double scroll_percent = (double)scroll_offset / (double)max_scroll;
			int thumb_pos = (ch - thumb_height) * (1.0 - scroll_percent);

			for (int i = 0; i < ch; ++i) {
				uint32_t cell_char = SCROLLBAR_LINE;
				uint16_t fg = TB_DEFAULT;
				if (i >= thumb_pos && i < thumb_pos + thumb_height) {
					cell_char = SCROLLBAR_THUMB;
					fg = TB_WHITE;
				}
				tb_set_cell(x + w - 1, cy + i, cell_char, fg, TB_DEFAULT);
			}
		}
	}
}

void draw_status_bar(SBProcess &process, InputMode mode, int width, int height) {
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

	state_str += (mode == INPUT_MODE_NORMAL)
		? " | r=Run, b=Add breakpoint, p=Print, n=Step Over, s=Step Into, o=Step Out, c=Continue, q=Quit"
		: " | Enter=Confirm, Esc=Cancel";

	for (int x = 0; x < width; ++x) {
		tb_set_cell(x, height - 1, ' ', TB_BLACK, TB_WHITE);
	}

	draw_text(1, height - 1, TB_BLACK, TB_WHITE, state_str);
}

int main(int argc, char** argv) {
	std::vector<std::string> target_env;
	std::vector<std::string> debuggee_args;
	std::string target_path;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-e" && i + 1 < argc) {
			target_env.push_back(argv[++i]);
		} else if (arg == "--") {
			for (int j = i + 1; j < argc; ++j) {
				debuggee_args.push_back(argv[j]);
			}
			break;
		} else if (target_path.empty()) {
			target_path = arg;
		} else {
			debuggee_args.push_back(arg);
		}
	}

	if (target_path.empty()) {
		std::cerr << "Usage: " << argv[0] << " [-e KEY=VALUE] ... <target_executable> [-- arg1 arg2 ...]\n";
		return 1;
	}

	int log_fd = open("tdbg.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (log_fd != -1) {
		dup2(log_fd, STDERR_FILENO);
		close(log_fd);
	}

	LLDBGuard lldb_guard;
	SBDebugger debugger = SBDebugger::Create();
	debugger.SetAsync(false); 

	SBTarget target = debugger.CreateTarget(target_path.c_str());
	if (!target.IsValid()) {
		std::cerr << "Failed to create target for " << target_path << "\n";
		return 1;
	}

	SBProcess process; 
	SBThread thread;

	TermboxGuard tb_guard;

	bool running = true;
	InputMode mode = INPUT_MODE_NORMAL;
	std::string input_buffer;
	std::string current_source_filename;
	std::vector<std::string> log_buffer;
	int log_scroll_offset = 0;
	int locals_scroll_offset = 0;
	int source_scroll_offset = 0;
	uint64_t last_pc = 0;
	SourceCache source_cache;
	log_buffer.push_back("Debugger started. Press 'b' to add breakpoint, 'r' to run.");

	tb_set_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE);

	while (running) {
		tb_clear();

		int width = tb_width();
		int height = tb_height();
		int main_window_height = height - layout_config.log_height - layout_config.status_height;
		int split_x = width - layout_config.sidebar_width;
		int locals_window_height = main_window_height - layout_config.breakpoints_height;

		SBFrame frame;
		if (process.IsValid() && process.GetState() != eStateExited) {
			thread = process.GetSelectedThread();
			if (thread.IsValid()) {
				frame = thread.GetSelectedFrame();
				if (frame.IsValid()) {
					uint64_t current_pc = frame.GetPC();
					if (current_pc != last_pc) {
						last_pc = current_pc;
						SBLineEntry le = frame.GetLineEntry();
						if (le.IsValid()) {
							std::string fullpath;
							if (le.GetFileSpec().GetDirectory()) {
								fullpath = std::string(le.GetFileSpec().GetDirectory()) + "/" + le.GetFileSpec().GetFilename();
								current_source_filename = le.GetFileSpec().GetFilename();
							} else {
								fullpath = le.GetFileSpec().GetFilename();
								current_source_filename = fullpath;
							}
							const std::vector<std::string>& lines = source_cache.get_lines(fullpath);
							int total_lines = (int)lines.size();
							int ch = main_window_height - 2;
							source_scroll_offset = std::max(0, (int)le.GetLine() - ch / 2 - 1);
							if (source_scroll_offset + ch > total_lines) {
								source_scroll_offset = std::max(0, total_lines - ch);
							}
						}
					}
				}
			}
		}

		draw_source_view(frame, 0, 0, split_x, main_window_height, source_cache, source_scroll_offset);
		draw_variables_view(frame, split_x, 0, layout_config.sidebar_width, locals_window_height, locals_scroll_offset);
		draw_breakpoints_view(target, split_x, locals_window_height, layout_config.sidebar_width, layout_config.breakpoints_height);
		draw_log_view(0, main_window_height, width, layout_config.log_height, log_buffer, mode, input_buffer, log_scroll_offset);
		draw_status_bar(process, mode, width, height);

		tb_present();

		struct tb_event ev;
		if (tb_poll_event(&ev) == 0) {
			if (ev.type == TB_EVENT_KEY) {
				if (mode == INPUT_MODE_NORMAL) {
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

							std::vector<const char*> launch_argv;
							launch_argv.push_back(target_path.c_str());
							for (const auto& arg : debuggee_args) {
								launch_argv.push_back(arg.c_str());
							}
							launch_argv.push_back(nullptr);

							std::vector<const char*> launch_env;
							for (const auto& env : target_env) {
								launch_env.push_back(env.c_str());
							}
							launch_env.push_back(nullptr);

							SBLaunchInfo launch_info(launch_argv.data());
							launch_info.SetEnvironmentEntries(launch_env.data(), true);
							launch_info.SetWorkingDirectory(".");

							SBError error;
							process = target.Launch(launch_info, error);

							if (!process.IsValid() || error.Fail()) {
								std::string err_msg = "Launch failed";
								if (error.GetCString()) {
									err_msg += ": ";
									err_msg += error.GetCString();
								}
								log_msg(log_buffer, err_msg);
							} else {
								log_msg(log_buffer, "Launched");
							}
						} else {
							log_msg(log_buffer, "Already running");
						}
					} else if (ev.ch == 'b') {
						mode = INPUT_MODE_BREAKPOINT;
						if (!current_source_filename.empty()) {
							input_buffer = current_source_filename + ":";
						} else {
							input_buffer.clear();
						}
					} else if (ev.ch == 'p') {
						mode = INPUT_MODE_VARIABLE;
						input_buffer.clear();
					} else {
						if (process.IsValid() && process.GetState() == eStateStopped) {
							switch (ev.ch) {
								case 'n': if (thread.IsValid()) thread.StepOver(); break;
								case 's': if (thread.IsValid()) thread.StepInto(); break;
								case 'o': if (thread.IsValid()) thread.StepOut(); break;
								case 'c': process.Continue(); break;
								case '<': layout_config.sidebar_width = std::min(width - 20, layout_config.sidebar_width + 2); break;
								case '>': layout_config.sidebar_width = std::max(20, layout_config.sidebar_width - 2); break;
							}
						} else {
							switch (ev.ch) {
								case '<': layout_config.sidebar_width = std::min(width - 20, layout_config.sidebar_width + 2); break;
								case '>': layout_config.sidebar_width = std::max(20, layout_config.sidebar_width - 2); break;
							}
						}
					}
				} else if (mode == INPUT_MODE_BREAKPOINT || mode == INPUT_MODE_VARIABLE) {
					if (ev.key == TB_KEY_ESC) {
						mode = INPUT_MODE_NORMAL;
						input_buffer.clear();
					} else if (ev.key == TB_KEY_ENTER) {
						if (!input_buffer.empty()) {
							if (mode == INPUT_MODE_BREAKPOINT) {
								SBBreakpoint bp = create_breakpoint(target, input_buffer);
								if (bp.IsValid() && bp.GetNumLocations() > 0) {
									log_msg(log_buffer, "Breakpoint added: " + input_buffer);
								} else {
									log_msg(log_buffer, "Failed/Invalid breakpoint: " + input_buffer);
								}
							} else if (mode == INPUT_MODE_VARIABLE) {
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
						mode = INPUT_MODE_NORMAL;
						input_buffer.clear();
					} else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
						if (!input_buffer.empty()) input_buffer.pop_back();
					} else if (ev.ch != 0) {
						input_buffer += (char)ev.ch;
					}
				}
			} else if (ev.type == TB_EVENT_MOUSE) {
				int main_window_height = tb_height() - layout_config.log_height - layout_config.status_height;

				// Log window scrolling
				int log_start_y = main_window_height;
				int log_end_y = tb_height() - layout_config.status_height;
				if (ev.y >= log_start_y && ev.y < log_end_y) {
					if (ev.key == TB_KEY_MOUSE_WHEEL_UP) {
						int max_scroll = std::max(0, (int)log_buffer.size() - (layout_config.log_height - 2));
						if (log_scroll_offset < max_scroll) {
							log_scroll_offset++;
						}
					} else if (ev.key == TB_KEY_MOUSE_WHEEL_DOWN) {
						if (log_scroll_offset > 0) {
							log_scroll_offset--;
						}
					}
				}

				// Source window scrolling
				if (ev.x < split_x && ev.y < main_window_height) {
					SBLineEntry le = frame.GetLineEntry();
					if (le.IsValid()) {
						std::string fullpath;
						if (le.GetFileSpec().GetDirectory()) {
							fullpath = std::string(le.GetFileSpec().GetDirectory()) + "/" + le.GetFileSpec().GetFilename();
						} else {
							fullpath = le.GetFileSpec().GetFilename();
						}
						const std::vector<std::string>& lines = source_cache.get_lines(fullpath);
						int total_lines = (int)lines.size();
						int ch = main_window_height - 2;
						int max_scroll = std::max(0, total_lines - ch);

						if (ev.key == TB_KEY_MOUSE_WHEEL_UP) {
							if (source_scroll_offset > 0) {
								source_scroll_offset--;
							}
						} else if (ev.key == TB_KEY_MOUSE_WHEEL_DOWN) {
							if (source_scroll_offset < max_scroll) {
								source_scroll_offset++;
							}
						}
					}
				}

				// Locals window scrolling
				int split_x = tb_width() - layout_config.sidebar_width;
				int locals_window_height = main_window_height - layout_config.breakpoints_height;
				if (ev.x >= split_x && ev.y < locals_window_height) {
					std::vector<VarLine> lines;
					if (frame.IsValid()) {
						SBValueList vars = frame.GetVariables(true, true, false, true);
						for (uint32_t i = 0; i < vars.GetSize(); ++i) {
							collect_variables_recursive(vars.GetValueAtIndex(i), 0, lines, layout_config.sidebar_width - 2);
						}
					}
					int max_scroll = std::max(0, (int)lines.size() - (locals_window_height - 2));

					if (ev.key == TB_KEY_MOUSE_WHEEL_UP) {
						if (locals_scroll_offset > 0) {
							locals_scroll_offset--;
						}
					} else if (ev.key == TB_KEY_MOUSE_WHEEL_DOWN) {
						if (locals_scroll_offset < max_scroll) {
							locals_scroll_offset++;
						}
					}
				}
			}
		}
	}

	return 0;
}
