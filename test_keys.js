// Test script for virtual Keys (emulateKeyPress → wasPressed / buttonCode / signal)
// Run from TRIK Studio while trikGui is visible via VNC.
// Click virtual panel buttons (OK, ▲, ▼, etc.) and watch the output.
//
// wasPressed(code) — возвращает true если клавиша была нажата с момента последнего
//                    вызова wasPressed() для этого кода. Самоочищается: повторный
//                    вызов вернёт false, пока кнопку не нажмут снова.
//
// buttonCode(wait)  — блокируется до любого нажатия, возвращает код клавиши.
//                    wait=true: ждёт пока не нажмут (пример: "жми любую кнопку").
//                    wait=false: опрашивает, возвращает -1 если ничего не нажато.
//
// buttonPressed     — сигнал, прилетает в любой момент (асинхронно).
//                    Подписываешься один раз, ловишь события без опроса.

function testWasPressed() {
    brick.display().clear();
    brick.display().addLabel("wasPressed test:", 1, 1, 8, 3);
    brick.display().addLabel("click OK", 2, 2, 6, 3);
    brick.display().redraw();

    var key_enter = 16777220;
    var key_up    = 16777235;
    var key_down  = 16777237;
    var key_esc   = 16777216;

    brick.message("Click a virtual button in VNC (wasPressed test)...");

    var timeout = 30000;
    var start = new Date().getTime();

    while (new Date().getTime() - start < timeout) {
        // wasPressed() самоочищается — один клик = одно срабатывание
        if (brick.keys().wasPressed(key_enter)) {
            brick.display().addLabel("  OK pressed!  ", 3, 2, 6, 4);
            brick.message("OK was pressed via virtual panel");
            break;
        }
        if (brick.keys().wasPressed(key_up)) {
            brick.display().addLabel("  UP pressed!  ", 3, 2, 6, 4);
            brick.message("UP was pressed");
            break;
        }
        if (brick.keys().wasPressed(key_down)) {
            brick.display().addLabel("  DOWN pressed!", 3, 2, 6, 4);
            brick.message("DOWN was pressed");
            break;
        }
        if (brick.keys().wasPressed(key_esc)) {
            brick.display().addLabel("  ESC pressed! ", 3, 2, 6, 4);
            brick.message("ESCAPE was pressed");
            break;
        }
        wait(50);
    }

    brick.display().redraw();
    brick.message("wasPressed test finished");
}

function testButtonCode() {
    brick.display().clear();
    brick.display().addLabel("buttonCode test:", 1, 1, 8, 3);
    brick.display().addLabel("press any key...", 2, 2, 8, 3);
    brick.display().redraw();

    brick.message("Waiting for any key press (buttonCode)...");

    // buttonCode(true) блокирует поток скрипта до нажатия любой кнопки.
    // Внутри Keys ждёт на QWaitCondition — не тратит CPU.
    var code = brick.keys().buttonCode(true);

    brick.display().addLabel("Got key code: " + code, 3, 2, 8, 4);
    brick.display().redraw();
    brick.message("buttonCode returned: " + code);
}

function testSignal() {
    brick.display().clear();
    brick.display().addLabel("Signal test:", 1, 1, 8, 3);
    brick.display().addLabel("press any key...", 2, 2, 8, 3);
    brick.display().redraw();
    brick.message("Signal test: clicking keys prints to console (F5)");

    // buttonPressed сигнал приходит асинхронно, без опроса.
    // В QtScript подписка:
    brick.keys().buttonPressed.connect(function(code, value) {
        brick.display().addLabel("Signal: code=" + code, 3, 2, 8, 4);
        brick.display().redraw();
        brick.message("buttonPressed signal: code=" + code + ", value=" + value);
    });

    // Ждём 15 секунд, ловим сигналы
    brick.display().addLabel("listening 15s...", 4, 2, 8, 3);
    brick.display().redraw();
    wait(15000);
    brick.message("Signal test finished");
}

function wait(ms) {
    var start = new Date().getTime();
    while (new Date().getTime() - start < ms) {}
}

// === Run ===
testWasPressed();
wait(1000);
testButtonCode();
wait(1000);
testSignal();