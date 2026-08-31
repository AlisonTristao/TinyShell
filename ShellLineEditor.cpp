#include "ShellLineEditor.h"

#include "ShellStyle.h"

// The wire protocol this speaks, both directions, is a plain VT100 subset:
//   in   printable bytes; DEL(0x7f)/BS(0x08) erase; CR or LF submits the
//        line; TAB completes; Ctrl+R (0x12) reverse-searches; ESC [ A/B/C/D
//        are up/down/left/right.
//   out  the typed characters echoed back; "\b \b" for an erase; a '\r' plus
//        a reprint of the line for a full redraw (history recall, wrap).
// With TINYSHELL_COLOR the output also carries standard SGR foreground/reset
// sequences for the command tokens. No cursor-addressing CSI is emitted, so
// the far terminal still only needs '\r', '\b' and overwrite-at-cursor for
// editing.

namespace {

void trimInPlace(std::string& s) {
    static const char* const kWs = " \t\r\n\v\f";
    const std::size_t begin = s.find_first_not_of(kWs);
    if (begin == std::string::npos) {
        s.clear();
        return;
    }
    const std::size_t end = s.find_last_not_of(kWs);
    s = s.substr(begin, end - begin + 1);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ShellLineEditor::ShellLineEditor(std::size_t historyCapacity) : capacity_(historyCapacity) {
    if (capacity_ == 0) {
        capacity_ = 1;
    }
    if (capacity_ > MAX_HISTORY_STORAGE) {
        capacity_ = MAX_HISTORY_STORAGE;
    }
    historyStorage_.resize(capacity_);
}

void ShellLineEditor::reset() {
    inputBuffer_.clear();
    draftBeforeHistory_.clear();
    pendingInput_.clear();
    renderedLength_ = 0;
    cursorIndex_ = 0;
    escState_ = EscState::None;
    activeInputStyle_ = InputStyle::Default;
    ignoreNextLf_ = false;
    historyCursor_ = -1;
    resetTabCycle();
    resetReverseSearch();
}

void ShellLineEditor::emit(char c) {
    if (sink_ != nullptr) {
        sink_->push_back(c);
    }
}

void ShellLineEditor::emit(const char* s) {
    if (sink_ != nullptr && s != nullptr) {
        sink_->append(s);
    }
}

void ShellLineEditor::emit(const std::string& s) {
    if (sink_ != nullptr) {
        sink_->append(s);
    }
}

void ShellLineEditor::setInputStyle(InputStyle style) {
    if (style == activeInputStyle_) {
        return;
    }

    const char* sgr = "";
    switch (style) {
        case InputStyle::Module:
            sgr = shell_sgr(ShellMsgClass::InputModule);
            break;
        case InputStyle::Function:
            sgr = shell_sgr(ShellMsgClass::InputFunction);
            break;
        case InputStyle::Argument:
            sgr = shell_sgr(ShellMsgClass::InputArgument);
            break;
        case InputStyle::Default:
        default:
            sgr = shell_sgr_reset();
            break;
    }
    emit(sgr);
    activeInputStyle_ = style;
}

void ShellLineEditor::resetInputStyle() {
    setInputStyle(InputStyle::Default);
}

void ShellLineEditor::emitStyledInputChar(char c, std::size_t inputIndex) {
    setInputStyle(inputStyleAt(inputIndex));
    emit(c);
}

void ShellLineEditor::emitVisibleInput(const std::string& visibleInput, std::size_t count,
                                       std::size_t inputStart, bool leftTruncated,
                                       bool rightTruncated) {
    if (count > visibleInput.size()) {
        count = visibleInput.size();
    }

    std::size_t tokenIndex = 0;
    bool sawToken = false;
    bool inToken = false;
    auto consumeStyle = [&](char ch) -> InputStyle {
        const bool whitespace = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                                ch == '\v' || ch == '\f';
        if (whitespace) {
            inToken = false;
            return InputStyle::Argument;
        }
        if (!inToken) {
            if (sawToken) {
                ++tokenIndex;
            } else {
                sawToken = true;
            }
            inToken = true;
        }
        return tokenIndex == 0 ? InputStyle::Module
                              : (tokenIndex == 1 ? InputStyle::Function
                                                 : InputStyle::Argument);
    };

    // Prime the tokenizer once up to the left edge of a horizontally scrolled
    // window. The visible run is then O(inputStart + visible columns), rather
    // than rescanning the whole command separately for every rendered byte.
    for (std::size_t i = 0; i < inputStart; ++i) {
        (void)consumeStyle(inputBuffer_[i]);
    }

    const std::size_t sourceOffset = leftTruncated ? 1U : 0U;
    for (std::size_t i = 0; i < count; ++i) {
        const bool isLeftMarker = leftTruncated && i == 0;
        const bool isRightMarker = rightTruncated && i + 1 == visibleInput.size();
        if (isLeftMarker || isRightMarker) {
            resetInputStyle();
            emit(visibleInput[i]);
            continue;
        }
        const std::size_t inputIndex = inputStart + i - sourceOffset;
        setInputStyle(consumeStyle(inputBuffer_[inputIndex]));
        emit(visibleInput[i]);
    }
}

void ShellLineEditor::feed(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0 || pendingInput_.size() >= MAX_PENDING_INPUT) {
        return;
    }
    const std::size_t room = MAX_PENDING_INPUT - pendingInput_.size();
    pendingInput_.append(reinterpret_cast<const char*>(data), (len < room) ? len : room);
}

void ShellLineEditor::feed(const std::string& data) {
    feed(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

bool ShellLineEditor::poll(std::string& out, std::string* outLine) {
    sink_ = &out;
    activeInputStyle_ = InputStyle::Default;

    std::string line;
    bool lineReady = false;
    std::size_t consumed = 0;
    while (consumed < pendingInput_.size()) {
        const char c = pendingInput_[consumed];
        ++consumed;
        processChar(c, lineReady, line);
        if (lineReady) {
            break;
        }
    }
    pendingInput_.erase(0, consumed);

    resetInputStyle();
    sink_ = nullptr;
    if (outLine != nullptr) {
        *outLine = lineReady ? line : std::string();
    }
    return lineReady;
}

void ShellLineEditor::setPrompt(const std::string& text) {
    prompt_ = text;
}

void ShellLineEditor::setPrompt(const std::string& text, std::string& out) {
    prompt_ = text;
    sink_ = &out;
    activeInputStyle_ = InputStyle::Default;
    redrawInput();
    resetInputStyle();
    sink_ = nullptr;
}

void ShellLineEditor::refreshLine(std::string& out) {
    sink_ = &out;
    activeInputStyle_ = InputStyle::Default;
    redrawInput();
    resetInputStyle();
    sink_ = nullptr;
}

// Append `text` to the current sink with CR+LF normalisation: a lone '\n'
// becomes "\r\n", an existing "\r\n" is preserved, a lone '\r' is left as a
// bare carriage return (ShellOutput::renderResponse emits one per line, "\r" +
// prefix + text + "\r\n" -- turning it into "\r\n" would inject a blank line),
// and a closing "\r\n" is added when `text` does not already end on a line
// break. Caller must have set sink_.
void ShellLineEditor::emitResponseBody(const std::string& text) {
    if (text.empty()) {
        return;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                continue;  // emitted on the '\n' below
            }
            emit('\r');
        } else if (c == '\n') {
            emit("\r\n");
        } else {
            emit(c);
        }
    }
    const char last = text.back();
    if (last != '\n' && last != '\r') {
        emit("\r\n");
    }
}

void ShellLineEditor::writeResponse(const std::string& text, std::string& out) {
    sink_ = &out;
    activeInputStyle_ = InputStyle::Default;

    emitResponseBody(text);

    // The response left the cursor at column 0 of a fresh line; there is
    // nothing on this line to overwrite when the prompt is repainted.
    renderedLength_ = 0;
    redrawInput();

    resetInputStyle();
    sink_ = nullptr;
}

void ShellLineEditor::writeAsyncOutput(const std::string& text, std::string& out) {
    if (text.empty()) {
        return;
    }
    sink_ = &out;
    activeInputStyle_ = InputStyle::Default;

    // Move off the prompt/input line so the text starts on its own line -- but
    // only on the first call of a burst. A run of these then sees
    // renderedLength_ == 0 (left that way below) and stacks with no gap.
    if (renderedLength_ > 0) {
        emit("\r\n");
        renderedLength_ = 0;
    }

    emitResponseBody(text);

    // Cursor sits at column 0 of a fresh line; the prompt is deliberately NOT
    // repainted here (see the header doc) so consecutive bursts do not put a
    // prompt between every line. Call refreshLine() once the burst pauses.
    resetInputStyle();
    sink_ = nullptr;
}

void ShellLineEditor::addHistory(const std::string& line) {
    pushHistory(line);
}

void ShellLineEditor::setCompletionProvider(CompletionProvider provider) {
    completionProvider_ = std::move(provider);
    resetTabCycle();
}

std::size_t ShellLineEditor::historyCount() const {
    return count_;
}

bool ShellLineEditor::historyAt(std::size_t index, std::string& outMessage) const {
    if (index >= count_) {
        outMessage.clear();
        return false;
    }
    outMessage = historyByOffset(index);
    return true;
}

bool ShellLineEditor::hasUnclosedDoubleQuote() const {
    bool inDoubleQuote = false;
    bool escaped = false;

    for (std::size_t i = 0; i < inputBuffer_.size(); ++i) {
        const char ch = inputBuffer_[i];

        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inDoubleQuote = !inDoubleQuote;
        }
    }

    return inDoubleQuote;
}

bool ShellLineEditor::fitsSingleRenderLine() const {
    return (prompt_.size() + inputBuffer_.size()) <= MAX_RENDER_COLUMNS;
}

ShellLineEditor::InputStyle ShellLineEditor::inputStyleAt(std::size_t inputIndex) const {
    if (inputIndex >= inputBuffer_.size()) {
        return InputStyle::Default;
    }

    std::size_t tokenIndex = 0;
    bool sawToken = false;
    bool inToken = false;
    for (std::size_t i = 0; i <= inputIndex; ++i) {
        const char ch = inputBuffer_[i];
        const bool whitespace = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                                ch == '\v' || ch == '\f';
        if (whitespace) {
            inToken = false;
            if (i == inputIndex) {
                return InputStyle::Argument;
            }
            continue;
        }
        if (!inToken) {
            if (sawToken) {
                ++tokenIndex;
            } else {
                sawToken = true;
            }
            inToken = true;
        }
    }

    if (tokenIndex == 0) {
        return InputStyle::Module;
    }
    if (tokenIndex == 1) {
        return InputStyle::Function;
    }
    return InputStyle::Argument;
}

void ShellLineEditor::buildVisibleInput(std::string& visibleInput, std::size_t& cursorInVisible,
                                        std::size_t& inputStart, bool& leftTruncated,
                                        bool& rightTruncated) const {
    const std::size_t promptLen = prompt_.size();
    std::size_t maxInputColumns = 8;
    if (MAX_RENDER_COLUMNS > promptLen) {
        maxInputColumns = MAX_RENDER_COLUMNS - promptLen;
    }

    const std::size_t inputLen = inputBuffer_.size();
    if (inputLen <= maxInputColumns) {
        visibleInput = inputBuffer_;
        cursorInVisible = cursorIndex_;
        inputStart = 0;
        leftTruncated = false;
        rightTruncated = false;
        return;
    }

    const std::size_t coreColumns = (maxInputColumns >= 3) ? (maxInputColumns - 2) : 1;
    std::size_t start = 0;

    if (cursorIndex_ > coreColumns / 2) {
        start = cursorIndex_ - (coreColumns / 2);
    }
    if (start + coreColumns > inputLen) {
        start = inputLen - coreColumns;
    }

    const std::size_t end = start + coreColumns;
    leftTruncated = (start > 0);
    rightTruncated = (end < inputLen);
    inputStart = start;

    visibleInput.clear();
    if (leftTruncated) {
        visibleInput += '<';
    }
    visibleInput += inputBuffer_.substr(start, end - start);
    if (rightTruncated) {
        visibleInput += '>';
    }

    cursorInVisible = (leftTruncated ? 1U : 0U) + (cursorIndex_ - start);
    if (cursorInVisible > visibleInput.size()) {
        cursorInVisible = visibleInput.size();
    }
}

void ShellLineEditor::processChar(char c, bool& lineReady, std::string& outLine) {
    // Parse ANSI escape sequences, especially arrow keys.
    if (escState_ != EscState::None) {
        if (escState_ == EscState::Esc) {
            escState_ = (c == '[') ? EscState::Bracket : EscState::None;
            return;
        }
        if (escState_ == EscState::Bracket) {
            if (c == 'A') {
                onArrowUp();
            } else if (c == 'B') {
                onArrowDown();
            } else if (c == 'C') {
                onArrowRight();
            } else if (c == 'D') {
                onArrowLeft();
            }
            escState_ = EscState::None;
            return;
        }
    }

    // Escape sequence introducer.
    if (c == 27) {
        resetTabCycle();
        resetReverseSearch();
        escState_ = EscState::Esc;
        return;
    }

    if (c == '\t') {
        onTab();
        return;
    }

    if (c == 18) {  // Ctrl+R
        onCtrlR();
        return;
    }

    // Finish command line on CR/LF and push non-empty command to history.
    if (c == '\r' || c == '\n') {
        if (c == '\n' && ignoreNextLf_) {
            ignoreNextLf_ = false;
            return;
        }

        if (hasUnclosedDoubleQuote()) {
            resetTabCycle();
            resetReverseSearch();
            ignoreNextLf_ = (c == '\r');

            if (inputBuffer_.size() < MAX_INPUT_LENGTH &&
                !(!inputBuffer_.empty() && inputBuffer_.back() == ' ')) {
                inputBuffer_ += ' ';
            }

            cursorIndex_ = inputBuffer_.size();
            renderedLength_ = prompt_.size() + inputBuffer_.size();
            return;
        }

        ignoreNextLf_ = (c == '\r');

        std::string line = inputBuffer_;
        trimInPlace(line);

        resetInputStyle();
        emit("\r\n");

        inputBuffer_.clear();
        draftBeforeHistory_.clear();
        historyCursor_ = -1;
        renderedLength_ = 0;
        cursorIndex_ = 0;
        resetTabCycle();
        resetReverseSearch();

        if (!line.empty()) {
            pushHistory(line);
            outLine = line;
            lineReady = true;
        }
        return;
    }

    ignoreNextLf_ = false;

    // Handle both backspace variants from terminals.
    if (c == 8 || c == 127) {
        eraseLastChar();
        return;
    }

    // Append printable characters.
    if (static_cast<std::uint8_t>(c) >= 32) {
        if (inputBuffer_.size() >= MAX_INPUT_LENGTH) {
            return;
        }

        resetTabCycle();
        resetReverseSearch();

        if (historyCursor_ >= 0) {
            historyCursor_ = -1;
            draftBeforeHistory_.clear();
        }

        const bool appendAtTail = (cursorIndex_ >= inputBuffer_.size());
        if (appendAtTail) {
            inputBuffer_ += c;
            ++cursorIndex_;

            if (fitsSingleRenderLine()) {
                emitStyledInputChar(c, cursorIndex_ - 1);
                renderedLength_ = prompt_.size() + inputBuffer_.size();
                return;
            }

            redrawInput();
            return;
        }

        const std::string left = inputBuffer_.substr(0, cursorIndex_);
        const std::string right = inputBuffer_.substr(cursorIndex_);
        inputBuffer_ = left + c + right;
        ++cursorIndex_;
        redrawInput();
    }
}

void ShellLineEditor::redrawInput() {
    std::string visibleInput;
    std::size_t cursorInVisible = 0;
    std::size_t inputStart = 0;
    bool leftTruncated = false;
    bool rightTruncated = false;
    buildVisibleInput(visibleInput, cursorInVisible, inputStart, leftTruncated, rightTruncated);

    const std::string visibleLine = prompt_ + visibleInput;

    resetInputStyle();
    emit('\r');
    emit(prompt_);
    emitVisibleInput(visibleInput, visibleInput.size(), inputStart, leftTruncated, rightTruncated);
    resetInputStyle();

    if (renderedLength_ > visibleLine.size()) {
        const std::size_t extra = renderedLength_ - visibleLine.size();
        for (std::size_t i = 0; i < extra; ++i) {
            emit(' ');
        }
    }

    emit('\r');
    emit(prompt_);
    emitVisibleInput(visibleInput, cursorInVisible, inputStart, leftTruncated, rightTruncated);
    resetInputStyle();

    renderedLength_ = visibleLine.size();
}

void ShellLineEditor::eraseLastChar() {
    if (inputBuffer_.empty() || cursorIndex_ == 0) {
        return;
    }

    resetTabCycle();
    resetReverseSearch();

    const bool removeAtTail = (cursorIndex_ == inputBuffer_.size());
    if (removeAtTail && fitsSingleRenderLine()) {
        inputBuffer_.erase(cursorIndex_ - 1, 1);
        --cursorIndex_;
        historyCursor_ = -1;

        emit("\b \b");
        if (renderedLength_ > 0) {
            --renderedLength_;
        }
        return;
    }

    inputBuffer_.erase(cursorIndex_ - 1, 1);
    --cursorIndex_;
    historyCursor_ = -1;
    redrawInput();
}

void ShellLineEditor::onArrowUp() {
    resetTabCycle();
    resetReverseSearch();

    if (count_ == 0) {
        return;
    }

    if (historyCursor_ < 0) {
        draftBeforeHistory_ = inputBuffer_;
        historyCursor_ = static_cast<int>(count_) - 1;
    } else if (historyCursor_ > 0) {
        --historyCursor_;
    }

    inputBuffer_ = historyByOffset(static_cast<std::size_t>(historyCursor_));
    cursorIndex_ = inputBuffer_.size();
    redrawInput();
}

void ShellLineEditor::onArrowDown() {
    resetTabCycle();
    resetReverseSearch();

    if (count_ == 0 || historyCursor_ < 0) {
        return;
    }

    if (historyCursor_ < static_cast<int>(count_) - 1) {
        ++historyCursor_;
        inputBuffer_ = historyByOffset(static_cast<std::size_t>(historyCursor_));
    } else {
        historyCursor_ = -1;
        inputBuffer_ = draftBeforeHistory_;
    }

    cursorIndex_ = inputBuffer_.size();
    redrawInput();
}

void ShellLineEditor::onArrowLeft() {
    resetTabCycle();
    resetReverseSearch();

    if (cursorIndex_ == 0) {
        return;
    }

    --cursorIndex_;

    if (fitsSingleRenderLine()) {
        emit('\b');
    } else {
        redrawInput();
    }
}

void ShellLineEditor::onArrowRight() {
    resetTabCycle();
    resetReverseSearch();

    if (cursorIndex_ >= inputBuffer_.size()) {
        return;
    }

    if (fitsSingleRenderLine()) {
        emitStyledInputChar(inputBuffer_[cursorIndex_], cursorIndex_);
        ++cursorIndex_;
    } else {
        ++cursorIndex_;
        redrawInput();
    }
}

void ShellLineEditor::onTab() {
    resetReverseSearch();

    if (!completionProvider_) {
        return;
    }

    if (!tabCycleActive_) {
        tabSeedInput_ = inputBuffer_;
        tabSuggestionCount_ =
            completionProvider_(tabSeedInput_, tabSuggestions_, MAX_TAB_SUGGESTIONS);
        // Trust the count no further than the buffer it was allowed to fill.
        if (tabSuggestionCount_ > MAX_TAB_SUGGESTIONS) {
            tabSuggestionCount_ = MAX_TAB_SUGGESTIONS;
        }
        tabSuggestionIndex_ = 0;

        if (tabSuggestionCount_ == 0) {
            return;
        }

        tabCycleActive_ = true;
    } else if (tabSuggestionCount_ > 1) {
        tabSuggestionIndex_ = (tabSuggestionIndex_ + 1) % tabSuggestionCount_;
    }

    const std::string nextInput = tabSuggestions_[tabSuggestionIndex_];
    if (nextInput.size() > MAX_INPUT_LENGTH) {
        return;
    }

    inputBuffer_ = nextInput;
    cursorIndex_ = inputBuffer_.size();
    historyCursor_ = -1;
    redrawInput();
}

void ShellLineEditor::onCtrlR() {
    resetTabCycle();

    if (count_ == 0) {
        return;
    }

    if (!reverseSearchActive_) {
        reverseSearchPrefix_ = inputBuffer_;
        reverseSearchLogicalIndex_ = static_cast<int>(count_);
        reverseSearchActive_ = true;
    }

    const std::string excludedText = inputBuffer_;
    int nextMatch =
        findPreviousHistoryMatch(reverseSearchPrefix_, reverseSearchLogicalIndex_, excludedText);
    if (nextMatch < 0) {
        nextMatch = findPreviousHistoryMatch(reverseSearchPrefix_, static_cast<int>(count_),
                                             excludedText);
    }

    if (nextMatch < 0) {
        return;
    }

    reverseSearchLogicalIndex_ = nextMatch;
    historyCursor_ = nextMatch;
    inputBuffer_ = historyByOffset(static_cast<std::size_t>(nextMatch));
    cursorIndex_ = inputBuffer_.size();
    redrawInput();
}

void ShellLineEditor::resetTabCycle() {
    tabSeedInput_.clear();
    tabSuggestionCount_ = 0;
    tabSuggestionIndex_ = 0;
    tabCycleActive_ = false;
}

void ShellLineEditor::resetReverseSearch() {
    reverseSearchPrefix_.clear();
    reverseSearchLogicalIndex_ = -1;
    reverseSearchActive_ = false;
}

int ShellLineEditor::findPreviousHistoryMatch(const std::string& prefix, int startExclusive,
                                              const std::string& excludedText) const {
    if (count_ == 0) {
        return -1;
    }

    int start = startExclusive;
    if (start > static_cast<int>(count_)) {
        start = static_cast<int>(count_);
    }
    if (start < 0) {
        start = 0;
    }

    for (int i = start - 1; i >= 0; --i) {
        const std::string candidate = historyByOffset(static_cast<std::size_t>(i));
        if ((prefix.empty() || startsWith(candidate, prefix)) && candidate != excludedText) {
            return i;
        }
    }

    return -1;
}

void ShellLineEditor::pushHistory(const std::string& message) {
    std::string normalized = message;
    trimInPlace(normalized);
    if (normalized.empty()) {
        return;
    }

    // Skip back-to-back duplicates so re-sending the same command does not
    // clutter arrow-key history with repeated entries.
    if (count_ > 0) {
        const std::size_t lastIndex = (firstIndex_ + count_ - 1) % capacity_;
        if (historyStorage_[lastIndex] == normalized) {
            return;
        }
    }

    if (count_ < capacity_) {
        const std::size_t insertIndex = (firstIndex_ + count_) % capacity_;
        historyStorage_[insertIndex] = normalized;
        ++count_;
        return;
    }

    historyStorage_[firstIndex_] = normalized;
    firstIndex_ = (firstIndex_ + 1) % capacity_;
}

std::string ShellLineEditor::historyByOffset(std::size_t offset) const {
    if (offset >= count_) {
        return std::string();
    }
    const std::size_t index = (firstIndex_ + offset) % capacity_;
    return historyStorage_[index];
}
