#ifndef SHELL_LINE_EDITOR_H
#define SHELL_LINE_EDITOR_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Server-side line editor for an interactive shell reached over an
 * opaque, bidirectional byte stream.
 *
 * It gives a remote terminal the editing a local one gets from its TTY
 * driver -- character echo, both backspace variants, in-line cursor movement
 * (left/right arrows), a circular command history (up/down arrows), Ctrl+R
 * reverse search, Tab completion through a caller-supplied provider, and a
 * horizontal scroll window for lines longer than the render width -- without
 * the terminal itself doing any of it. The terminal stays a "dumb" device:
 * it forwards raw keystrokes in and prints raw bytes out.
 *
 * That makes it a natural companion to TinyShell over a transport that only
 * moves bytes (a UART, a socket, a framed radio/USB channel): feed the bytes
 * the terminal sent, poll() out finished command lines to hand to
 * TinyShell::run_command_line(), and send back everything the editor echoed.
 *
 * It is framework-free -- only <string>/<vector>/<functional> -- so it links
 * and unit-tests on a host, and it has no dependency on the TinyShell class
 * (Tab completion is a std::function you provide, e.g. one backed by
 * TinyShell::complete_line).
 *
 * NOT thread-safe: drive one instance from a single thread/task. Everything
 * it would echo is appended to a std::string you own, so no Stream, no
 * blocking, no I/O of its own.
 *
 * When TINYSHELL_COLOR is enabled, live input is token-coloured with standard
 * ANSI SGR: module dark yellow, function dark blue, arguments in the default
 * foreground colour (readable on light and dark backgrounds). The
 * escape bytes never count toward cursor columns and each public output batch
 * restores the default rendition before returning.
 *
 * Usage:
 *   ShellLineEditor ed;
 *   std::string out;
 *   ed.setPrompt("$ ", out);            // paints the first prompt into `out`
 *   ...
 *   ed.feed(rxBytes, n);                // buffer raw terminal input
 *   std::string line;
 *   while (ed.poll(out, &line)) {       // drain it; `out` gets echo/redraws
 *       run_command_line(line);
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
     * echoes anything on its own. Bytes past MAX_PENDING_INPUT of unprocessed
     * backlog are dropped (a terminal that floods faster than poll() drains
     * is pathological); call poll() promptly after feed().
     */
    void feed(const std::uint8_t* data, std::size_t len);
    void feed(const std::string& data);

    /**
     * @brief Process buffered input. Everything the editor echoes (typed
     * characters, backspace erases, history recall, prompt redraws, and any
     * enabled token-colour SGR) is
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
     * @brief Emit @p text as output that arrived asynchronously while the user
     * sits at the prompt -- an unsolicited log line, a background task's
     * periodic result.
     *
     * Breaks off the current prompt/input line if one is on screen, applies the
     * same CR+LF normalisation as writeResponse(), and leaves the cursor at
     * column 0 of a fresh line WITHOUT repainting the prompt -- so a run of
     * these stacks cleanly with no prompt between the lines. Call refreshLine()
     * once the burst pauses to bring the edit line back.
     */
    void writeAsyncOutput(const std::string& text, std::string& out);

    /**
     * @brief Draw a status line that later calls overwrite in place -- a
     * progress bar (see shell_format_progress()), a spinner, a countdown.
     *
     * The first call moves off the prompt/input line if one is on screen; each
     * call then returns to column 0 with '\r', prints @p line and pads with
     * spaces over whatever a longer previous line left behind. No newline is
     * sent, so successive calls update one line. Only the text up to the first
     * '\r' or '\n' of @p line is used; its SGR escapes do not count as columns.
     *
     * The line is closed (left visible, cursor moved to a fresh line) by the
     * next writeResponse(), writeAsyncOutput(), refreshLine() or setPrompt()
     * that repaints, or by poll() as soon as there is typed input to echo --
     * nothing ever draws over it.
     */
    void writeProgress(const std::string& line, std::string& out);

    /**
     * @brief Seed arrow-up history without echoing anything (e.g. replaying
     * persisted history at boot).
     */
    void addHistory(const std::string& line);

    void setCompletionProvider(CompletionProvider provider);

    // Introspection, handy for tests and for a "show history" command.
    std::size_t historyCount() const;
    bool historyAt(std::size_t index, std::string& outMessage) const;

private:
    // Defensive ceiling for the per-instance history ring; the real size is
    // the constructor's historyCapacity clamped to this.
    static constexpr std::size_t MAX_HISTORY_STORAGE = 128;
    // Longest editable line, in bytes (a UTF-8 line is measured in bytes).
    static constexpr std::size_t MAX_INPUT_LENGTH = 512;
    // Render width in columns; a longer line scrolls horizontally.
    static constexpr std::size_t MAX_RENDER_COLUMNS = 72;
    static constexpr std::size_t MAX_TAB_SUGGESTIONS = 24;
    // Cap on unprocessed feed() backlog; excess is dropped.
    static constexpr std::size_t MAX_PENDING_INPUT = 4096;

    enum class EscState : std::uint8_t { None, Esc, Bracket };
    enum class InputStyle : std::uint8_t { Default, Module, Function, Argument };

    // Output sink for the current public call: set on entry, cleared on exit.
    // Every private helper appends echo/redraw bytes through emit() rather
    // than doing any I/O of its own.
    std::string* sink_ = nullptr;
    void emit(char c);
    void emit(const char* s);
    void emit(const std::string& s);
    void setInputStyle(InputStyle style);
    void resetInputStyle();
    void emitStyledInputChar(char c, std::size_t inputIndex);
    void emitVisibleInput(const std::string& visibleInput, std::size_t count,
                          std::size_t inputStart, bool leftTruncated,
                          bool rightTruncated);
    // Shared body of writeResponse()/writeAsyncOutput(): CR+LF-normalised
    // append of `text` to sink_, with a closing "\r\n" when it lacks one.
    void emitResponseBody(const std::string& text);
    // Leave a writeProgress() line on screen and move to a fresh line. No-op
    // when none is shown. Caller must have set sink_.
    void closeProgress();

    std::string pendingInput_;  // raw bytes fed but not yet processed by poll()

    std::string inputBuffer_;
    std::string prompt_;
    std::string draftBeforeHistory_;
    std::size_t renderedLength_ = 0;
    std::size_t cursorIndex_ = 0;
    EscState escState_ = EscState::None;
    InputStyle activeInputStyle_ = InputStyle::Default;
    bool ignoreNextLf_ = false;
    bool progressShown_ = false;        // a writeProgress() line is on screen
    std::size_t progressColumns_ = 0;   // its visible width, for padding
    bool progressHidPrompt_ = false;    // it broke off a painted prompt line

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
    InputStyle inputStyleAt(std::size_t inputIndex) const;
    void buildVisibleInput(std::string& visibleInput, std::size_t& cursorInVisible,
                           std::size_t& inputStart, bool& leftTruncated,
                           bool& rightTruncated) const;
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
