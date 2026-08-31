// Standalone smoke test for ShellLineEditor -- plain asserts, no framework.
// To run it:
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

std::string stripSgr(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\x1b') {
            out += s[i];
            continue;
        }
        if (i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) {
                ++i;
            }
        }
    }
    return out;
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
    assert(stripSgr(out) == "he");  // per-char echo while the line fits one render row
}

void test_command_tokens_use_semantic_echo_colours() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);

    out.clear();
    ed.feed(std::string("dongle -ping 7"));
    std::string line;
    assert(!ed.poll(out, &line));
#ifdef TINYSHELL_COLOR
    assert(out == "\x1b[33mdongle\x1b[30m \x1b[34m-ping\x1b[30m 7\x1b[0m");
#else
    assert(out == "dongle -ping 7");
#endif
}

void test_each_echo_batch_resets_its_colour() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);

    out.clear();
    ed.feed(std::string("d"));
    ed.poll(out, nullptr);
#ifdef TINYSHELL_COLOR
    assert(out == "\x1b[33md\x1b[0m");
#else
    assert(out == "d");
#endif

    out.clear();
    ed.feed(std::string("o"));
    ed.poll(out, nullptr);
#ifdef TINYSHELL_COLOR
    assert(out == "\x1b[33mo\x1b[0m");
#else
    assert(out == "o");
#endif
}

void test_history_redraw_preserves_command_token_colours() {
    ShellLineEditor ed(8);
    std::string out;
    ed.setPrompt("$ ", out);
    ed.addHistory("dongle -ping 7");

    out.clear();
    ed.feed(std::string("\x1b[A"));
    ed.poll(out, nullptr);
    assert(stripSgr(out).find("$ dongle -ping 7") != std::string::npos);
#ifdef TINYSHELL_COLOR
    assert(out.find("\x1b[33mdongle") != std::string::npos);
    assert(out.find("\x1b[34m-ping") != std::string::npos);
    assert(out.find("\x1b[30m 7") != std::string::npos);
#else
    assert(out.find('\x1b') == std::string::npos);
#endif
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
    assert(stripSgr(out) == "\r$ help \r$ help ");
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
    assert(stripSgr(out) == "info");

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

void test_write_response_keeps_lone_cr_as_carriage_return() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);

    // ShellOutput::renderResponse() builds each line as "\r" + prefix + text
    // + "\r\n". The leading lone '\r' must stay a carriage return, not become
    // a "\r\n" that shows up as a blank line before every response line.
    out.clear();
    ed.writeResponse("\r< done\r\n", out);
    assert(out == "\r< done\r\n\r$ \r$ ");
}

void test_write_async_output_breaks_off_prompt_then_stacks() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);
    ed.feed(std::string("ab"));
    ed.poll(out, nullptr);  // prompt + "ab" now on screen

    // First async line: breaks off the "$ ab" line, prints the text, and
    // leaves the cursor at column 0 -- the prompt is NOT repainted.
    out.clear();
    ed.writeAsyncOutput("tick 1", out);
    assert(out == "\r\ntick 1\r\n");

    // Second line in the same burst: no further break, it just stacks.
    out.clear();
    ed.writeAsyncOutput("tick 2", out);
    assert(out == "tick 2\r\n");

    // The burst pauses: refreshLine() brings the "$ ab" edit line back intact.
    out.clear();
    ed.refreshLine(out);
    assert(vis(stripSgr(out)).find("$ ab") != std::string::npos);

    // The in-progress input survived -- CR still completes "ab".
    ed.feed(std::string("\r"));
    std::string l;
    assert(ed.poll(out, &l));
    assert(l == "ab");
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
    assert(vis(stripSgr(out)).find("sensor -imu_status") != std::string::npos);
}

void test_long_line_scrolls_and_still_completes() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);

    std::string cmd(200, 'x');  // far wider than the render window
    ed.feed(cmd + "\r");
    std::string l;
    assert(ed.poll(out, &l));
    assert(l == cmd);  // every byte survived the scroll-window redraws
}

void test_feed_backlog_is_capped_not_unbounded() {
    ShellLineEditor ed(4);
    std::string out;
    ed.setPrompt("$ ", out);

    // Flood far past MAX_PENDING_INPUT (4096) with no CR, then poll.
    ed.feed(std::string(20000, 'a'));
    std::string l;
    assert(!ed.poll(out, &l));           // no line completed
    ed.feed(std::string("\r"));
    assert(ed.poll(out, &l));
    assert(l.size() <= 512);             // clamped to MAX_INPUT_LENGTH, not 20000
}

}  // namespace

int main() {
    test_prompt_and_fast_path_echo();
    test_command_tokens_use_semantic_echo_colours();
    test_each_echo_batch_resets_its_colour();
    test_history_redraw_preserves_command_token_colours();
    test_tab_completion();
    test_enter_yields_line_and_history();
    test_backspace_fast_path();
    test_write_response_normalises_newlines();
    test_write_response_keeps_lone_cr_as_carriage_return();
    test_write_async_output_breaks_off_prompt_then_stacks();
    test_multi_line_feed_drains_by_looping_poll();
    test_unclosed_quote_continues_line();
    test_reverse_search();
    test_long_line_scrolls_and_still_completes();
    test_feed_backlog_is_capped_not_unbounded();
    std::printf("ShellLineEditor: all smoke tests passed\n");
    return 0;
}
