// Standalone smoke test for ShellLineEditor. The TinyShell repo has no test
// framework of its own; the firmware repos that consume this library
// (bally_dongle, bally_OS) exercise it under PlatformIO + Unity. This file is
// a quick local check. To run it:
//   g++ -std=gnu++17 -I. -Wall -Wextra ShellLineEditor.cpp test/test_shell_line_editor.cpp -o sle
//   ./sle

#include "ShellLineEditor.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string vis(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\r') {
            o += "\\r";
        } else if (c == '\n') {
            o += "\\n";
        } else if (c == '\b') {
            o += "\\b";
        } else if (c == 27) {
            o += "\\e";
        } else {
            o += c;
        }
    }
    return o;
}

void test_prompt_and_fast_path_echo() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);
    assert(out == "\r$ \r$ ");

    out.clear();
    ed.feed(std::string("he"));
    std::string line;
    assert(!ed.poll(out, &line));
    assert(out == "he");  // per-char echo while the line fits one render row
}

void test_tab_completion() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);
    ed.setCompletionProvider(
        [](const std::string& in, std::string* sug, std::size_t max) -> std::size_t {
            if (in == "he" && max > 0) {
                sug[0] = "help ";
                return 1;
            }
            return 0;
        });

    ed.feed(std::string("he"));
    std::string line;
    out.clear();
    ed.poll(out, &line);

    out.clear();
    ed.feed(std::string("\t"));
    assert(!ed.poll(out, &line));
    assert(out == "\r$ help \r$ help ");
}

void test_enter_yields_line_and_history() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);

    // Type the line, then send the CR separately so `out` isolates what the
    // Enter itself emits (just CRLF -- the caller repaints the prompt after).
    ed.feed(std::string("info"));
    out.clear();
    std::string line;
    assert(!ed.poll(out, &line));
    assert(out == "info");

    ed.feed(std::string("\r"));
    out.clear();
    assert(ed.poll(out, &line));
    assert(line == "info");
    assert(out == "\r\n");

    assert(ed.historyCount() == 1);
    std::string h;
    assert(ed.historyAt(0, h) && h == "info");
}

void test_backspace_fast_path() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("> ", out);
    ed.feed(std::string("ab"));
    ed.poll(out, nullptr);

    out.clear();
    ed.feed(std::string("\x7f"));  // DEL
    ed.poll(out, nullptr);
    assert(out == "\b \b");
}

void test_write_response_normalises_newlines() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);

    out.clear();
    ed.writeResponse("line1\nline2", out);
    assert(out == "line1\r\nline2\r\n\r$ \r$ ");
}

void test_multi_line_feed_drains_by_looping_poll() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);
    ed.feed(std::string("one\rtwo\r"));

    std::vector<std::string> lines;
    std::string l;
    while (ed.poll(out, &l)) {
        lines.push_back(l);
    }
    assert(lines.size() == 2 && lines[0] == "one" && lines[1] == "two");
}

void test_unclosed_quote_continues_line() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);
    ed.feed(std::string("echo \"hi\r"));

    std::string l;
    assert(!ed.poll(out, &l));  // the CR does not complete the line
    ed.feed(std::string("there\"\r"));
    assert(ed.poll(out, &l));
    assert(l == "echo \"hi there\"");
}

void test_reverse_search() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);
    ed.addHistory("sensor -debug");
    ed.addHistory("link -stats");
    ed.addHistory("sensor -imu_status");

    ed.feed(std::string("sen"));
    ed.poll(out, nullptr);
    out.clear();
    ed.feed(std::string("\x12"));  // Ctrl+R -> most recent "sen*" match
    ed.poll(out, nullptr);
    assert(vis(out).find("sensor -imu_status") != std::string::npos);
}

}  // namespace

int main() {
    test_prompt_and_fast_path_echo();
    test_tab_completion();
    test_enter_yields_line_and_history();
    test_backspace_fast_path();
    test_write_response_normalises_newlines();
    test_multi_line_feed_drains_by_looping_poll();
    test_unclosed_quote_continues_line();
    test_reverse_search();
    std::printf("ShellLineEditor: all smoke tests passed\n");
    return 0;
}
