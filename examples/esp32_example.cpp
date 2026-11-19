#include <Arduino.h>
#include <TinyShell.h>

// create your shell instance
TinyShell ts;

    /*
    * the syntax to send commands to the shell is:
    * <module> -<command> [args]
    * 
    * where:
    * <module> is the name of the module
    * -command is the name of the command
    * [args] are the arguments for the command
    * 
    * example:
    * teste -t1 1, 2, 3
    * 
    * module: teste
    * command: t1
    * args: 1, 2, 3

    * if your function returns uint8_t, it will be printed in the shell
    * if your function returns other types, you need to wrap your function
    * 
    * the return uint8_t (byte) represents the status of the command:
    * 0 = success
    * 255 = error
    * 
    * you can create types if you want to use them in your functions
    * the definition of the types is done in the header file TableLinker.h
    */

// wrapper functions to be used in the shell
uint8_t wrapper_e() {
    // explain the command usage
    Serial.println("Usage: <module> -<command> [args]");
    Serial.println("Example: teste -t1 1, 2, 3");  
    return RESULT_OK;  // return 0 to indicate success
}

// example function to be added to the shell
uint8_t teste_1(int a, char b, uint8_t c) {
    Serial.print("Teste 1 called with args: ");
    Serial.print(a);
    Serial.print(", ");
    Serial.print(b);
    Serial.print(", ");
    Serial.print(c);
    Serial.println();
    return RESULT_OK;  // return 0 to indicate success
}

uint8_t teste_2(int a, int b, uint8_t c) {
    Serial.print("Teste 2 called with args: ");
    Serial.print(a);
    Serial.print(", ");
    Serial.print(b);
    Serial.print(", ");
    Serial.print(c);
    Serial.println();
    return RESULT_ERROR;  // return 255 to indicate an error
}

// ---------------------------------------------------------------------------
// Progress bar. `slow_job` is the kind of function that lives in another
// library: it knows nothing about the shell, it just reports its progress to
// an optional plain C callback.
using ProgressFn = void (*)(void* ctx, uint8_t percent);

uint8_t slow_job(int steps, ProgressFn on_progress, void* ctx) {
    for (int i = 1; i <= steps; ++i) {
        delay(50);  // the real work
        if (on_progress) on_progress(ctx, static_cast<uint8_t>(i * 100 / steps));
    }
    return RESULT_OK;
}

// The sink: redraws one line with '\r' (needs a terminal that honours it --
// PuTTY, picocom, idf.py monitor; not the Arduino IDE Serial Monitor).
void serial_progress(void* label, uint8_t percent) {
    char line[80];
    shell_format_progress(line, sizeof(line), percent, static_cast<const char*>(label));
    Serial.print('\r');
    Serial.print(line);
    if (percent >= 100) Serial.println();
}

void setup() {
    Serial.begin(921600);
    delay(1000);

    // every response the shell produces goes here -- a std::string_view
    // straight out of the shell's own buffer, no copy
    ts.set_output_callback([](std::string_view text) {
        Serial.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    });

    // registers the built-in "help" module: help -modules, help -types
    ts.begin();

    // create the modules
    ts.create_module("teste", "Funcoes de teste com texto");

    // add the functions to the modules
    ts.add(teste_1, "t1", "Teste de funcao com 3 parametros", "teste");
    ts.add(teste_2, "t2", "Teste de funcao com 3 parametros", "teste");

    // add the wrapper function to the (built-in) help module
    ts.add(wrapper_e, "e", "Explica o uso do comando", "help");

    // progress bar: the lambda is the only place that knows both the job and
    // the terminal -- the shell sees `demo -progress (i4)`
    ts.create_module("demo", "Terminal demos");
    ts.add([](int steps) { return slow_job(steps, serial_progress, (void*)"job"); },
           "progress", "Run a job with a progress bar: demo -progress 40", "demo");
}

String commandBuffer = "";

void loop() {

    // the code below reads commands from the serial port
    // it has nothing to do with the shell itself
    // you can remove it if you want to use the shell in another way
    // you just need to send a string formatted as "<module> -<command> [args]\n"

    while (Serial.available()) {
        char c = Serial.read();

        // detects if the character is a control character
        if (c == 8 || c == 127) {  // backspace/delete
            if (!commandBuffer.isEmpty()) {
                commandBuffer.remove(commandBuffer.length() - 1);

                // deletes the last character from the serial output
                Serial.print("\b \b");
            }
            continue;
        }

        // echo the character to the serial output
        Serial.write(c);

        // end of command detection
        if (c == '\n') {
            commandBuffer.trim();  // remove leading/trailing whitespace
            Serial.println();      // new line for better readability

            Serial.println("Received command: " + commandBuffer);

            // run the command in the shell; its messages arrive through the
            // output callback above, the return value is the status code
            const uint8_t status = ts.run_command_line(commandBuffer.c_str());
            Serial.print("status: ");
            Serial.println(status);

            commandBuffer = "";
        } else {
            commandBuffer += c;
        }
    }

    delay(10);
}