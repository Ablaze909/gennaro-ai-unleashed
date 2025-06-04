/*
 * GENNARO AI - OFFICIAL FIRMWARE VERSION
 * Simple, reliable, works 100%
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <furi_hal_uart.h>
#include <furi_hal_console.h>

#define TAG "GennaroAI"
#define UART_CH (FuriHalUartIdUSART1)

typedef enum {
    GennaroAIViewSubmenu,
    GennaroAIViewConsole,
    GennaroAIViewTextInput,
} GennaroAIView;

typedef enum {
    GennaroAIMenuConsole,
    GennaroAIMenuVision,
    GennaroAIMenuMath,
    GennaroAIMenuOCR,
    GennaroAIMenuStatus,
    GennaroAIMenuCustom,
} GennaroAIMenuItem;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    TextInput* text_input;
    NotificationApp* notifications;
    FuriString* console_text;
    FuriString* input_text;
} GennaroAIApp;

static void uart_on_irq_cb(UartIrqEvent ev, uint8_t data, void* context) {
    GennaroAIApp* app = context;
    if(ev == UartIrqEventRXNE && app && app->console_text) {
        if(data >= 32 || data == '\n' || data == '\r') {
            furi_string_push_back(app->console_text, data);
            if(furi_string_size(app->console_text) > 4096) {
                furi_string_right(app->console_text, 2048);
            }
        }
    }
}

static void send_command(GennaroAIApp* app, const char* cmd) {
    if(!app || !cmd) return;
    
    furi_string_cat_printf(app->console_text, "\n>>> %s\n", cmd);
    furi_hal_uart_tx(UART_CH, (uint8_t*)cmd, strlen(cmd));
    furi_hal_uart_tx(UART_CH, (uint8_t*)"\r\n", 2);
    
    if(app->notifications) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

static void submenu_callback(void* context, uint32_t index) {
    GennaroAIApp* app = context;
    
    switch(index) {
        case GennaroAIMenuConsole:
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
        case GennaroAIMenuVision:
            send_command(app, "VISION");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
        case GennaroAIMenuMath:
            send_command(app, "MATH");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
        case GennaroAIMenuOCR:
            send_command(app, "OCR");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
        case GennaroAIMenuStatus:
            send_command(app, "STATUS");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
            text_box_set_focus(app->text_box, TextBoxFocusEnd);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
            break;
        case GennaroAIMenuCustom:
            furi_string_reset(app->input_text);
            view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewTextInput);
            break;
    }
}

static void text_input_callback(void* context) {
    GennaroAIApp* app = context;
    const char* cmd = furi_string_get_cstr(app->input_text);
    if(strlen(cmd) > 0) {
        send_command(app, cmd);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->console_text));
        text_box_set_focus(app->text_box, TextBoxFocusEnd);
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewConsole);
    }
}

static bool console_input_callback(InputEvent* event, void* context) {
    GennaroAIApp* app = context;
    if(event->type == InputTypePress && event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
        return true;
    }
    return false;
}

static uint32_t navigation_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t navigation_submenu_callback(void* context) {
    UNUSED(context);
    return GennaroAIViewSubmenu;
}

static GennaroAIApp* gennaro_ai_app_alloc() {
    GennaroAIApp* app = malloc(sizeof(GennaroAIApp));
    if(!app) return NULL;
    
    memset(app, 0, sizeof(GennaroAIApp));
    
    app->console_text = furi_string_alloc();
    app->input_text = furi_string_alloc();
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "📺 Console Monitor", GennaroAIMenuConsole, submenu_callback, app);
    submenu_add_item(app->submenu, "👁️ VISION Analysis", GennaroAIMenuVision, submenu_callback, app);
    submenu_add_item(app->submenu, "🧮 MATH Solver", GennaroAIMenuMath, submenu_callback, app);
    submenu_add_item(app->submenu, "📝 OCR Text", GennaroAIMenuOCR, submenu_callback, app);
    submenu_add_item(app->submenu, "📊 STATUS Check", GennaroAIMenuStatus, submenu_callback, app);
    submenu_add_item(app->submenu, "⌨️ Custom Command", GennaroAIMenuCustom, submenu_callback, app);
    
    view_set_previous_callback(submenu_get_view(app->submenu), navigation_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewSubmenu, submenu_get_view(app->submenu));
    
    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), navigation_submenu_callback);
    view_set_input_callback(text_box_get_view(app->text_box), console_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewConsole, text_box_get_view(app->text_box));
    
    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Send command to ESP32-CAM");
    text_input_set_result_callback(app->text_input, text_input_callback, app, app->input_text, 32, true);
    view_set_previous_callback(text_input_get_view(app->text_input), navigation_submenu_callback);
    view_dispatcher_add_view(app->view_dispatcher, GennaroAIViewTextInput, text_input_get_view(app->text_input));
    
    furi_string_cat_str(app->console_text, 
        "=== GENNARO AI - ESP32-CAM Assistant ===\n"
        "UART @ 115200 baud ready\n"
        "Use menu for AI commands\n\n");
    
    furi_hal_console_disable();
    furi_hal_uart_set_br(UART_CH, 115200);
    furi_hal_uart_set_irq_cb(UART_CH, uart_on_irq_cb, app);
    
    return app;
}

static void gennaro_ai_app_free(GennaroAIApp* app) {
    if(!app) return;
    
    furi_hal_uart_set_irq_cb(UART_CH, NULL, NULL);
    furi_hal_console_enable();
    
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewConsole);
    view_dispatcher_remove_view(app->view_dispatcher, GennaroAIViewTextInput);
    
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    text_input_free(app->text_input);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_string_free(app->console_text);
    furi_string_free(app->input_text);
    furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

int32_t gennaro_ai_app(void* p) {
    UNUSED(p);
    
    GennaroAIApp* app = gennaro_ai_app_alloc();
    if(!app) return -1;
    
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, GennaroAIViewSubmenu);
    
    if(app->notifications) {
        notification_message(app->notifications, &sequence_display_backlight_on);
        notification_message(app->notifications, &sequence_single_vibro);
    }
    
    view_dispatcher_run(app->view_dispatcher);
    
    furi_record_close(RECORD_GUI);
    gennaro_ai_app_free(app);
    
    return 0;
}
