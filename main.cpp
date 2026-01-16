#include <lldb/API/LLDB.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace lldb;

void print_variables(SBFrame &frame) {
	SBValueList vars = frame.GetVariables(true, true, false, true);

	for (uint32_t i = 0; i < vars.GetSize(); ++i) {
		SBValue var = vars.GetValueAtIndex(i);
		std::cout << var.GetName() << " = ";

		if (!var.IsValid()) {
			std::cout << "(invalid)\n";
		}
		else if (var.GetType().IsPointerType()) {
			uint64_t addr = var.GetValueAsUnsigned();
			if (addr == 0) {
				std::cout << "(null pointer)\n";
			} else {
				std::cout << "(pointer at 0x" << std::hex << addr << std::dec << ")\n";
			}
		}
		else if (var.GetNumChildren() > 0) {
			std::cout << "{ ";
			uint32_t n = var.GetNumChildren();
			for (uint32_t j = 0; j < n; ++j) {
				SBValue child = var.GetChildAtIndex(j);
				std::cout << child.GetName() << ": " << (child.IsValid() ? child.GetValue() : "(invalid)");
				if (j + 1 < n) std::cout << ", ";
			}
			std::cout << " }\n";
		} else {
			std::cout << var.GetValue() << "\n";
		}
	}
}


void print_backtrace(SBThread &thread) {
	int num_frames = thread.GetNumFrames();
	for (int i = 0; i < num_frames; ++i) {
		SBFrame frame = thread.GetFrameAtIndex(i);
		std::cout << "#" << i << " " << frame.GetFunctionName()
			<< " at line " << frame.GetLineEntry().GetLine() << "\n";
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <target_executable>\n";
		return 1;
	}

	const char* target_path = argv[1];

	// Initialize LLDB.
	SBDebugger::Initialize();
	SBDebugger debugger = SBDebugger::Create();
	debugger.SetAsync(true);

	SBTarget target = debugger.CreateTarget(target_path);
	if (!target.IsValid()) {
		std::cerr << "Failed to create target for " << target_path << "\n";
		return 1;
	}

	SBBreakpoint bp = target.BreakpointCreateByName("main");
	std::cout << "Breakpoint set at main\n";

	SBProcess process = target.LaunchSimple(nullptr, nullptr, ".");
	if (!process.IsValid()) {
		std::cerr << "Failed to launch process\n";
		return 1;
	}

	std::cout << "Process launched\n";

	SBListener listener = debugger.GetListener();
	SBEvent event;

	while (true) {
		// Wait for events from LLDB.
		if (listener.WaitForEvent(1, event)) {
			StateType state = SBProcess::GetStateFromEvent(event);

			switch (state) {
				case eStateStopped:
					{
						std::cout << "\nProcess stopped!\n";
						SBThread thread = process.GetSelectedThread();
						SBFrame frame = thread.GetFrameAtIndex(0);
						std::cout << "Stopped at function: " << frame.GetFunctionName()
							<< ", line: " << frame.GetLineEntry().GetLine() << "\n";

						// REPL loop for user commands.
						std::string cmd;
						while (true) {
							std::cout << "(tdbg) ";
							std::getline(std::cin, cmd);
							if (cmd == "c") {
								process.Continue();
								break; // exit REPL, wait for next stop
							} else if (cmd == "s") {
								thread.StepInto();
								break;
							} else if (cmd == "n") {
								thread.StepOver();
								break;
							} else if (cmd == "bt") {
								print_backtrace(thread);
							} else if (cmd == "v") {
								print_variables(frame);
							} else if (cmd == "q") {
								process.Kill();
								goto cleanup;
							} else {
								std::cout << "Commands: c=continue, s=step in, n=step over, bt=backtrace, v=variables, q=quit\n";
							}
						}
						break;
					}

				case eStateExited:
					{
						std::cout << "Process exited\n";
						goto cleanup;
					}

				case eStateRunning:
					break;

				default:
					break;
			}
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

cleanup:
	SBDebugger::Terminate();
	return 0;
}
