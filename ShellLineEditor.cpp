#include "ShellLineEditor.h"

// The wire protocol this speaks, both directions, is a plain VT100 subset:
//   in   printable bytes; DEL(0x7f)/BS(0x08) erase; CR or LF submits the
//        line; TAB completes; Ctrl+R (0x12) reverse-searches; ESC [ A/B/C/D
//        are up/down/left/right.
//   out  the typed characters echoed back; "\b \b" for an erase; a '\r' plus
//        a reprint of the line for a full redraw (history recall, wrap).
// No other CSI sequences are emitted, so the far terminal only needs to
// understand '\r', '\b' and overwrite-at-cursor.

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
    redrawInput();
    sink_ = nullptr;
}

void ShellLineEditor::refreshLine(std::string& out) {
    sink_ = &out;
    redrawInput();
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

    emitResponseBody(text);

    // The response left the cursor at column 0 of a fresh line; there is
    // nothing on this line to overwrite when the prompt is repainted.
    renderedLength_ = 0;
    redrawInput();

    sink_ = nullptr;
}

void ShellLineEditor::writeAsyncOutput(const std::string& text, std::string& out) {
    if (text.empty()) {
        return;
    }
    sink_ = &out;

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

void ShellLineEditor::buildVisibleInput(std::string& visibleInput, std::size_t& cursorInVisible) const {
    const std::size_t promptLen = prompt_.size();
    std::size_t maxInputColumns = 8;
    if (MAX_RENDER_COLUMNS > promptLen) {
        maxInputColumns = MAX_RENDER_COLUMNS - promptLen;
    }

    const std::size_t inputLen = inputBuffer_.size();
    if (inputLen <= maxInputColumns) {
        visibleInput = inputBuffer_;
        cursorInVisible = cursorIndex_;
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
    const bool leftTruncated = (start > 0);
    const bool rightTruncated = (end < inputLen);

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
                emit(c);
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
    buildVisibleInput(visibleInput, cursorInVisible);

    const std::string visibleLine = prompt_ + visibleInput;
    const std::size_t cursorColumn = prompt_.size() + cursorInVisible;

    emit('\r');
    emit(visibleLine);

    if (renderedLength_ > visibleLine.size()) {
        const std::size_t extra = renderedLength_ - visibleLine.size();
        for (std::size_t i = 0; i < extra; ++i) {
            emit(' ');
        }
    }

    emit('\r');
    for (std::size_t i = 0; i < cursorColumn && i < visibleLine.size(); ++i) {
        emit(visibleLine[i]);
    }

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
        emit(inputBuffer_[cursorIndex_]);
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
