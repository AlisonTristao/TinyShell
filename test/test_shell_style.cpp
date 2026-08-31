// Standalone smoke test for ShellStyle -- plain asserts, no framework.
// Compile it BOTH ways (the colour gate changes what the API emits):
//   g++ -std=gnu++17 -I. -Wall -Wextra ShellStyle.cpp test/test_shell_style.cpp -o ss  && ./ss
//   g++ -std=gnu++17 -I. -Wall -Wextra -DTINYSHELL_COLOR ShellStyle.cpp test/test_shell_style.cpp -o ssc && ./ssc

#include "ShellStyle.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

// -- classification (flag-independent) --------------------------------------

void test_classify_prefixes() {
    assert(shell_classify_line("erro(12/INVALID_ARGUMENT) faltou o mac") == ShellMsgClass::Error);
    assert(shell_classify_line("aviso(3/DATABASE) log nao persistido") == ShellMsgClass::Warning);
    assert(shell_classify_line("[help] dongle -ping") == ShellMsgClass::Help);
    assert(shell_classify_line("[error][aa:bb:cc:dd:ee:ff] peer sumiu") == ShellMsgClass::Error);
    assert(shell_classify_line("[info][aa:bb:cc:dd:ee:ff] ola") == ShellMsgClass::Muted);
    assert(shell_classify_line("[telemetry][aa:bb] v=3.1") == ShellMsgClass::Muted);
}

void test_classify_keywords() {
    assert(shell_classify_line("Module 'foo' not found.") == ShellMsgClass::Error);
    assert(shell_classify_line("Comando 'x' do modulo 'y' falhou com codigo de erro: 2.") ==
           ShellMsgClass::Error);
    assert(shell_classify_line("Comando 'x' do modulo 'y' executado com sucesso.") ==
           ShellMsgClass::Success);
    assert(shell_classify_line("led ligado") == ShellMsgClass::Success);
    assert(shell_classify_line("WARNING: deprecated flag") == ShellMsgClass::Warning);
    assert(shell_classify_line("uptime 1234 ms") == ShellMsgClass::Output);
    assert(shell_classify_line("") == ShellMsgClass::Output);
}

void test_classify_is_idempotent_on_coloured_text() {
    // A line already wrapped in SGR must classify by its text, not its escapes.
    assert(shell_classify_line("\x1b[31merro(1/X) boom\x1b[0m") == ShellMsgClass::Error);
    assert(shell_classify_line("\x1b[32mtudo certo\x1b[0m") == ShellMsgClass::Output);
}

// -- colorize_block: terminator / identity behaviour ------------------------

void test_colorize_block_preserves_terminators() {
    // Whatever the flag does to the content, the \r\n / \n / trailing-none
    // shape of the block is preserved exactly.
    const std::string in = "one\r\ntwo\nthree";
    const std::string out = shell_colorize_block(in);

    // strip SGR back out and we must land on the original bytes
    std::string stripped;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\x1b') {
            while (i < out.size() && out[i] != 'm') ++i;
            continue;
        }
        stripped += out[i];
    }
    assert(stripped == in);
}

void test_colorize_block_leaves_precoloured_lines() {
    const std::string in = "\x1b[35mcustom\x1b[0m\n";
    assert(shell_colorize_block(in, ShellMsgClass::Error) == in);
}

void test_colorize_block_fallback_only_hits_plain_lines() {
    // "detalhe" has no keyword -> fallback applies; "sucesso" keeps its own class.
    const std::string in = "detalhe\nsucesso ao salvar\n";
    const std::string out = shell_colorize_block(in, ShellMsgClass::Error);
#ifdef TINYSHELL_COLOR
    assert(out.find("\x1b[31mdetalhe\x1b[0m") != std::string::npos);
    assert(out.find("\x1b[32msucesso ao salvar\x1b[0m") != std::string::npos);
#else
    assert(out == in);
#endif
}

// -- the colour gate -------------------------------------------------------

void test_gate() {
#ifdef TINYSHELL_COLOR
    assert(std::string(shell_sgr(ShellMsgClass::Error)) == "\x1b[31m");
    assert(std::string(shell_sgr(ShellMsgClass::Output)).empty());  // Output never colours
    assert(std::string(shell_sgr_reset()) == "\x1b[0m");
    assert(shell_colorize_block("boom: erro fatal\n") == "\x1b[31mboom: erro fatal\x1b[0m\n");
#else
    assert(std::string(shell_sgr(ShellMsgClass::Error)).empty());
    assert(std::string(shell_sgr_reset()).empty());
    // Flag off => byte-for-byte identity, classifier notwithstanding.
    const std::string in = "boom: erro fatal\nok: led ligado\n[help] x\n";
    assert(shell_colorize_block(in, ShellMsgClass::Error) == in);
#endif
}

}  // namespace

int main() {
    test_classify_prefixes();
    test_classify_keywords();
    test_classify_is_idempotent_on_coloured_text();
    test_colorize_block_preserves_terminators();
    test_colorize_block_leaves_precoloured_lines();
    test_colorize_block_fallback_only_hits_plain_lines();
    test_gate();

#ifdef TINYSHELL_COLOR
    std::printf("ShellStyle: all smoke tests passed (colour ON)\n");
#else
    std::printf("ShellStyle: all smoke tests passed (colour OFF)\n");
#endif
    return 0;
}
