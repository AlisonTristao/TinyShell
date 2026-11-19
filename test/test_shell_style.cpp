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
    // TinyShell's own (English, since 2.2.0) status lines.
    assert(shell_classify_line("foo: module not found") == ShellMsgClass::Error);
    assert(shell_classify_line("robot -fail: failed with status 3") == ShellMsgClass::Error);
    assert(shell_classify_line("robot -pwm: success") == ShellMsgClass::Success);
    assert(shell_classify_line("led ligado") == ShellMsgClass::Success);
    assert(shell_classify_line("WARNING: deprecated flag") == ShellMsgClass::Warning);
    assert(shell_classify_line("uptime 1234 ms") == ShellMsgClass::Output);
    assert(shell_classify_line("") == ShellMsgClass::Output);
    assert(shell_classify_line("   \t  ") == ShellMsgClass::Output);
}

void test_classify_is_allocation_free_on_noisy_input() {
    // Leading whitespace + a CSI run before the real text -- logical_view()
    // must see past both without a strip/trim copy.
    assert(shell_classify_line("\x1b[2K\x1b[1;33m   aviso(9/X) algo") == ShellMsgClass::Warning);
    assert(shell_classify_line("  \x1b[31m[error][mac] caiu\x1b[0m  ") == ShellMsgClass::Error);
    // Case-insensitive prefixes and tags.
    assert(shell_classify_line("ERRO(1/X) boom") == ShellMsgClass::Error);
    assert(shell_classify_line("[HELP] modulos") == ShellMsgClass::Help);
    assert(shell_classify_line("[Error][mac] x") == ShellMsgClass::Error);
    // Already-coloured line still classifies by its text.
    assert(shell_classify_line("\x1b[32mtudo certo\x1b[0m") == ShellMsgClass::Output);
}

void test_classify_negation_guard() {
    // A line that denies the thing is not an error, even though it says "erro".
    assert(shell_classify_line("nenhum erro encontrado") == ShellMsgClass::Output);
    assert(shell_classify_line("sem falhas no ultimo ciclo") == ShellMsgClass::Output);
    assert(shell_classify_line("no error on channel B") == ShellMsgClass::Output);
    // But a structured prefix / tag is unambiguous -- the guard never applies.
    assert(shell_classify_line("erro(0/NONE) sem falha residual") == ShellMsgClass::Error);
    assert(shell_classify_line("[error][mac] sem resposta") == ShellMsgClass::Error);
}

void test_classify_custom_classifier_is_honoured() {
    const ShellLineClassifier everythingMuted = [](std::string_view) { return ShellMsgClass::Muted; };
    const std::string out = shell_colorize_block("erro(1/X) boom\n", ShellMsgClass::Output, everythingMuted);
#ifdef TINYSHELL_COLOR
    assert(out == "\x1b[90merro(1/X) boom\x1b[0m\n");  // grey, not red
#else
    assert(out == "erro(1/X) boom\n");
#endif
}

// -- shell_strip_sgr -------------------------------------------------------

void test_strip_sgr() {
    assert(shell_strip_sgr("\x1b[31mred\x1b[0m plain") == "red plain");
    assert(shell_strip_sgr("no escapes here") == "no escapes here");
    assert(shell_strip_sgr("a\x1b[1;33;2mb\x1b[0mc") == "abc");
    assert(shell_strip_sgr("trailing\x1b") == "trailing");
    assert(shell_strip_sgr(std::string_view("cut\x1b[3", 6)) == "cut");  // truncated CSI
}

// -- colorize_block ------------------------------------------------------

void test_colorize_block_preserves_terminators() {
    const std::string in = "one\r\ntwo\nthree";
    const std::string out = shell_colorize_block(in);

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
    const std::string in = "detalhe\nsucesso ao salvar\n";
    const std::string out = shell_colorize_block(in, ShellMsgClass::Error);
#ifdef TINYSHELL_COLOR
    assert(out.find("\x1b[31mdetalhe\x1b[0m") != std::string::npos);
    assert(out.find("\x1b[32msucesso ao salvar\x1b[0m") != std::string::npos);
#else
    assert(out == in);
#endif
}

void test_colorize_block_bounds_input() {
    // Beyond SHELL_MAX_COLORIZE_INPUT the block is returned untouched.
    std::string big(SHELL_MAX_COLORIZE_INPUT + 1, 'x');
    big += "\nerro(1/X) boom\n";
    assert(shell_colorize_block(big, ShellMsgClass::Error) == big);

    std::string justUnder(SHELL_MAX_COLORIZE_INPUT - 20, 'x');
    justUnder = "erro fatal\n" + justUnder;
    const std::string out = shell_colorize_block(justUnder);
#ifdef TINYSHELL_COLOR
    assert(out.rfind("\x1b[31merro fatal\x1b[0m\n", 0) == 0);
#else
    assert(out == justUnder);
#endif
}

// -- shell_format_progress -------------------------------------------------

std::string progress(unsigned pct, std::string_view label = {}, std::size_t width = 10) {
    char buf[128];
    const std::size_t n = shell_format_progress(buf, sizeof(buf), pct, label, width);
    assert(n == std::char_traits<char>::length(buf));
    return shell_strip_sgr(std::string(buf, n));
}

void test_progress_format() {
    assert(progress(0) == "  0% [          ]");
    assert(progress(5) == "  5% [          ]");             // less than one cell
    assert(progress(42) == " 42% [===>      ]");
    assert(progress(100) == "100% [==========]");            // complete: no '>' head
    assert(progress(250) == "100% [==========]");            // clamped
    assert(progress(50, "flash") == "flash  50% [====>     ]");
    assert(progress(50, {}, 0) == " 50% [ ]");               // width clamped to 1
    assert(progress(0, {}, SHELL_PROGRESS_WIDTH).size() == 7 + SHELL_PROGRESS_WIDTH);

#ifdef TINYSHELL_COLOR
    char buf[64];
    shell_format_progress(buf, sizeof(buf), 50, {}, 4);
    assert(std::string(buf) == " 50% [\x1b[32m=>\x1b[0m  ]");  // only the filled cells
    shell_format_progress(buf, sizeof(buf), 0, {}, 4);
    assert(std::string(buf) == "  0% [    ]");                  // nothing filled, no SGR
#endif
}

void test_progress_bounds_its_buffer() {
    char small[8];
    const std::size_t n = shell_format_progress(small, sizeof(small), 42, "label", 20);
    assert(n == sizeof(small) - 1);
    assert(small[n] == '\0');
    assert(std::string(small) == "label  ");

    char one[1] = {'x'};
    assert(shell_format_progress(one, sizeof(one), 42) == 0);
    assert(one[0] == '\0');
    assert(shell_format_progress(nullptr, 64, 42) == 0);  // no buffer: nothing written
}

// -- the colour gate -----------------------------------------------------

void test_gate() {
    assert(shell_color_enabled() == (shell_sgr(ShellMsgClass::Error)[0] != '\0'));
#ifdef TINYSHELL_COLOR
    assert(shell_color_enabled());
    assert(std::string(shell_sgr(ShellMsgClass::Error)) == "\x1b[31m");
    assert(std::string(shell_sgr(ShellMsgClass::Output)).empty());  // Output never colours
    assert(std::string(shell_sgr(ShellMsgClass::InputModule)) == "\x1b[33m");
    assert(std::string(shell_sgr(ShellMsgClass::InputFunction)) == "\x1b[34m");
    assert(std::string(shell_sgr(ShellMsgClass::InputArgument)) == "\x1b[39m");
    assert(std::string(shell_sgr_reset()) == "\x1b[0m");
    assert(shell_colorize_block("boom: erro fatal\n") == "\x1b[31mboom: erro fatal\x1b[0m\n");
#else
    assert(!shell_color_enabled());
    assert(std::string(shell_sgr(ShellMsgClass::Error)).empty());
    assert(std::string(shell_sgr(ShellMsgClass::InputModule)).empty());
    assert(std::string(shell_sgr(ShellMsgClass::InputFunction)).empty());
    assert(std::string(shell_sgr(ShellMsgClass::InputArgument)).empty());
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
    test_classify_is_allocation_free_on_noisy_input();
    test_classify_negation_guard();
    test_classify_custom_classifier_is_honoured();
    test_strip_sgr();
    test_colorize_block_preserves_terminators();
    test_colorize_block_leaves_precoloured_lines();
    test_colorize_block_fallback_only_hits_plain_lines();
    test_colorize_block_bounds_input();
    test_progress_format();
    test_progress_bounds_its_buffer();
    test_gate();

#ifdef TINYSHELL_COLOR
    std::printf("ShellStyle: all smoke tests passed (colour ON)\n");
#else
    std::printf("ShellStyle: all smoke tests passed (colour OFF)\n");
#endif
    return 0;
}
