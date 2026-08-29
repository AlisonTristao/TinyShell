#ifndef SHELL_LINE_EDITOR_H
#define SHELL_LINE_EDITOR_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Server-side line editor for an interactive shell reached over an
 * opaque byte stream -- a UART console, or a BTP TERMINAL_IN / TERMINAL_OUT
 * channel.
 *
 * Portable C++ port of bally_dongle's old lib/ShellSerial: identical
 * line-editing behaviour (echo, both backspace variants, in-line cursor
 * movement, circular history via ESC[A / ESC[B, Ctrl+R reverse search, Tab
 * completion through an external provider, a scroll window for long lines),
 * but it reads its input from feed()/poll() instead of an Arduino Stream and
 * writes every byte it would have echoed into a caller-owned std::string
 * instead of Stream::print(). No Arduino, no FreeRTOS -- links and unit-tests
 * on the host.
 *
 * Usage:
 *   ShellLineEditor ed;
 *   std::string out;
 *   ed.setPrompt("$ ", out);            // paints the first prompt into `out`
 *   ...
 *   ed.feed(rxBytes, n);                // buffer raw terminal input
 *   std::string line;
 *   while (ed.poll(out, &line)) {       // drain it; `out` gets echo/redraws
 *       runCommand(line);
 *   }
 *   ed.writeResponse(commandOutput, out);
 *   sendToTerminal(out);                // whatever `out` accumulated
 */
class ShellLineEditor {
public:
    using CompletionProvider = std::function<std::size_t(
        const std::string& input, std::string* outSuggestions, std::size_t maxSuggestions)>;

    static constexpr std::size_t DEFAULT_HISTORY_CAPACITY = 64;

    explicit ShellLineEditor(std::size_t historyCapacity = DEFAULT_HISTORY_CAPACITY);

    /**
     * @brief Drop the current input line, cursor and escape-parser state.
     * Prompt text and history are untouched. Call on a fresh session.
     */
    void reset();

    /**
     * @brief Buffer raw terminal input bytes for poll() to process. Never
     * echoes anything on its own.
     */
    void feed(const std::uint8_t* data, std::size_t len);
    void feed(const std::string& data);

    /**
     * @brief Process buffered input. Everything the editor echoes (typed
     * characters, backspace erases, history recall, prompt redraws) is
     * appended to @p out.
     *
     * @param outLine When non-null, receives the trimmed command line the
     *   moment one is entered (also pushed to history).
     * @return true when a command line completed on this call. Buffered input
     *   past that line stays for the next poll(); call again in a loop to
     *   drain several lines from one feed().
     */
    bool poll(std::string& out, std::string* outLine);

    /**
     * @brief Set the prompt shown before the editable input.
     * The overload taking @p out repaints the line; the one without only
     * stores the text (use it to seed the prompt before the first paint).
     */
    void setPrompt(const std::string& text);
    void setPrompt(const std::string& text, std::string& out);

    /**
     * @brief Repaint prompt + current input at the current cursor column.
     */
    void refreshLine(std::string& out);

    /**
     * @brief Emit @p text (a command's output) followed by a fresh prompt
     * line. Lone '\n' is normalised to "\r\n"; a trailing newline is added if
     * missing so the redrawn prompt starts on its own line.
     */
    void writeResponse(const std::string& text, std::string& out);

    /**
     * @brief Seed arrow-up history without echoing anything (e.g. replaying
     * persisted history at boot).
     */
    void addHistory(const std::string& line);

    void setCompletionProvider(CompletionProvider provider);

    // Introspection -- kept from ShellSerial, handy for tests/tools.
    std::size_t historyCount() const;
    bool historyAt(std::size_t index, std::string& outMessage) const;

private:
    // Defensive ceiling for the per-instance history ring; the real size is
    // the constructor's historyCapacity clamped to this.
    static constexpr std::size_t MAX_HISTORY_STORAGE = 128;
    static constexpr std::size_t MAX_INPUT_LENGTH = 512;
    static constexpr std::size_t MAX_RENDER_COLUMNS = 72;
    static constexpr std::size_t MAX_TAB_SUGGESTIONS = 24;

    enum class EscState : std::uint8_t { None, Esc, Bracket };

    // Output sink for the current public call. Set on entry, cleared on exit;
    // every private helper appends through emit() instead of ShellSerial's
    // `serial_->print`.
    std::string* sink_ = nullptr;
    void emit(char c);
    void emit(const char* s);
    void emit(const std::string& s);

    std::string pendingInput_;  // raw bytes fed but not yet processed by poll()

    std::string inputBuffer_;
    std::string prompt_;
    std::string draftBeforeHistory_;
    std::size_t renderedLength_ = 0;
    std::size_t cursorIndex_ = 0;
    EscState escState_ = EscState::None;
    bool ignoreNextLf_ = false;

    std::vector<std::string> historyStorage_;  // sized to capacity_
    std::size_t capacity_;
    std::size_t count_ = 0;
    std::size_t firstIndex_ = 0;
    int historyCursor_ = -1;

    CompletionProvider completionProvider_;
    std::string tabSeedInput_;
    std::string tabSuggestions_[MAX_TAB_SUGGESTIONS];
    std::size_t tabSuggestionCount_ = 0;
    std::size_t tabSuggestionIndex_ = 0;
    bool tabCycleActive_ = false;

    std::string reverseSearchPrefix_;
    int reverseSearchLogicalIndex_ = -1;
    bool reverseSearchActive_ = false;

    void processChar(char c, bool& lineReady, std::string& outLine);
    bool hasUnclosedDoubleQuote() const;
    bool fitsSingleRenderLine() const;
    void buildVisibleInput(std::string& visibleInput, std::size_t& cursorInVisible) const;
    void redrawInput();
    void eraseLastChar();
    void onArrowUp();
    void onArrowDown();
    void onArrowLeft();
    void onArrowRight();
    void onTab();
    void onCtrlR();
    void resetTabCycle();
    void resetReverseSearch();
    int findPreviousHistoryMatch(const std::string& prefix, int startExclusive,
                                 const std::string& excludedText) const;
    void pushHistory(const std::string& message);
    std::string historyByOffset(std::size_t offset) const;
};

#endif  // SHELL_LINE_EDITOR_H
