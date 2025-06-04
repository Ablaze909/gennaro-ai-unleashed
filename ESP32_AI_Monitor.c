#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/dialog_ex.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <expansion/expansion.h>
#include <cli/cli.h>

#define TAG "ESP32_AI_Monitor"
#define UART_CH (FuriHalSerialIdUsart)
#define BAUDRATE (115200)
#define RX_BUFFER_SIZE (2048)
#define RESPONSE_TIMEOUT (30000)

// Scenes
typedef enum {
    ESP32_AI_SceneMain,
    ESP32_AI_SceneVision,
    ESP32_AI_SceneMath, 
    ESP32_AI_SceneOCR,
    ESP32_AI_SceneCount,
    ESP32_AI_SceneStatus,
    ESP32_AI_SceneFlashControl,
    ESP32_AI_ScenePTTRecord,
    ESP32_AI_SceneResponse,
    ESP32_AI_SceneCount_
} ESP32_AI_Scene;

// Views
typedef enum {
    ESP32_AI_ViewSubmenu,
    ESP32_AI_ViewTextBox,
    ESP32_AI_ViewDialog,
} ESP32_AI_View;

// Events
typedef enum {
    ESP32_AI_EventVision,
    ESP32_AI_EventMath,
    ESP32_AI_EventOCR,
    ESP32_AI_EventCount,
    ESP32_AI_EventStatus,
    ESP32_AI_EventFlashOn,
    ESP32_AI_EventFlashOff,
    ESP32_AI_EventFlashToggle,
    ESP32_AI_EventPTTStart,
    ESP32_AI_EventPTTStop,
    ESP32_AI_EventBack,
} ESP32_AI_Event;

// App context
typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    TextBox* text_box;
    DialogEx* dialog_ex;
    NotificationApp* notifications;
    
    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    FuriThread* rx_thread;
    bool is_connected;
    bool ptt_active;
    
    FuriString* response_text;
    FuriString* status_text;
    bool flash_state;
} ESP32_AI_App;

// Function declarations
static void esp32_ai_app_uart_init(ESP32_AI_App* app);
static void esp32_ai_app_uart_deinit(ESP32_AI_App* app);
static void esp32_ai_app_send_command(ESP32_AI_App* app, const char* command);
static bool esp32_ai_app_wait_response(ESP32_AI_App* app, uint32_t timeout_ms);
static int32_t esp32_ai_app_rx_thread(void* context);

// UART receive thread
static int32_t esp32_ai_app_rx_thread(void* context) {
    ESP32_AI_App* app = context;
    uint8_t buffer[256];
    
    while(furi_thread_get_state(furi_thread_get_current()) == FuriThreadStateRunning) {
        size_t bytes_read = furi_hal_serial_rx(app->serial_handle, buffer, sizeof(buffer));
        if(bytes_read > 0) {
            furi_stream_buffer_send(app->rx_stream, buffer, bytes_read, 0);
        }
        furi_delay_ms(10);
    }
    
    return 0;
}

// UART initialization
static void esp32_ai_app_uart_init(ESP32_AI_App* app) {
    app->serial_handle = furi_hal_serial_control_acquire(UART_CH);
    if(app->serial_handle) {
        furi_hal_serial_init(app->serial_handle, BAUDRATE);
        
        app->rx_stream = furi_stream_buffer_alloc(RX_BUFFER_SIZE, 1);
        app->rx_thread = furi_thread_alloc_ex("ESP32_RX", 1024, esp32_ai_app_rx_thread, app);
        furi_thread_start(app->rx_thread);
        
        app->is_connected = true;
        FURI_LOG_I(TAG, "UART initialized on pins 13(TX), 14(RX) at %d baud", BAUDRATE);
        
        // Test connection
        esp32_ai_app_send_command(app, "STATUS");
    } else {
        app->is_connected = false;
        FURI_LOG_E(TAG, "Failed to acquire UART");
    }
}

// UART cleanup
static void esp32_ai_app_uart_deinit(ESP32_AI_App* app) {
    if(app->rx_thread) {
        furi_thread_interrupt(app->rx_thread);
        furi_thread_join(app->rx_thread);
        furi_thread_free(app->rx_thread);
    }
    
    if(app->rx_stream) {
        furi_stream_buffer_free(app->rx_stream);
    }
    
    if(app->serial_handle) {
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
    }
    
    app->is_connected = false;
}

// Send command to ESP32
static void esp32_ai_app_send_command(ESP32_AI_App* app, const char* command) {
    if(!app->is_connected || !app->serial_handle) {
        FURI_LOG_W(TAG, "UART not connected");
        return;
    }
    
    FURI_LOG_I(TAG, "Sending: %s", command);
    furi_hal_serial_tx(app->serial_handle, (uint8_t*)command, strlen(command));
    furi_hal_serial_tx(app->serial_handle, (uint8_t*)"\n", 1);
    
    // Clear previous response
    furi_string_reset(app->response_text);
    
    // Visual feedback
    notification_message(app->notifications, &sequence_single_vibro);
}

// Wait for and read response
static bool esp32_ai_app_wait_response(ESP32_AI_App* app, uint32_t timeout_ms) {
    if(!app->is_connected) return false;
    
    uint32_t start_time = furi_get_tick();
    uint8_t buffer[256];
    
    while((furi_get_tick() - start_time) < timeout_ms) {
        size_t bytes_received = furi_stream_buffer_receive(
            app->rx_stream, buffer, sizeof(buffer) - 1, 100);
            
        if(bytes_received > 0) {
            buffer[bytes_received] = '\0';
            furi_string_cat_str(app->response_text, (char*)buffer);
            
            // Check if we have a complete response
            if(furi_string_search_str(app->response_text, "\n") != FURI_STRING_FAILURE ||
               furi_string_search_str(app->response_text, "OK:") != FURI_STRING_FAILURE ||
               furi_string_search_str(app->response_text, "ERROR:") != FURI_STRING_FAILURE ||
               furi_string_search_str(app->response_text, "READY") != FURI_STRING_FAILURE) {
                return true;
            }
        }
    }
    
    if(furi_string_empty(app->response_text)) {
        furi_string_set_str(app->response_text, "TIMEOUT: No response from ESP32");
    }
    
    return false;
}

// Scene: Main Menu
void esp32_ai_scene_main_on_enter(void* context) {
    ESP32_AI_App* app = context;
    Submenu* submenu = app->submenu;
    
    submenu_reset(submenu);
    submenu_set_header(submenu, "ESP32-CAM AI Monitor");
    
    submenu_add_item(submenu, "📷 Vision Analysis", ESP32_AI_EventVision, NULL);
    submenu_add_item(submenu, "🧮 Math Solver", ESP32_AI_EventMath, NULL);
    submenu_add_item(submenu, "📖 Text OCR", ESP32_AI_EventOCR, NULL);
    submenu_add_item(submenu, "🔢 Object Count", ESP32_AI_EventCount, NULL);
    submenu_add_item(submenu, "💡 Flash Control", ESP32_AI_EventFlashToggle, NULL);
    submenu_add_item(submenu, "🎤 Voice Record", ESP32_AI_EventPTTStart, NULL);
    submenu_add_item(submenu, "📊 System Status", ESP32_AI_EventStatus, NULL);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32_AI_ViewSubmenu);
}

bool esp32_ai_scene_main_on_event(void* context, SceneManagerEvent event) {
    ESP32_AI_App* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
            case ESP32_AI_EventVision:
                esp32_ai_app_send_command(app, "VISION");
                scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneResponse);
                consumed = true;
                break;
                
            case ESP32_AI_EventMath:
                esp32_ai_app_send_command(app, "MATH");
                scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneResponse);
                consumed = true;
                break;
                
            case ESP32_AI_EventOCR:
                esp32_ai_app_send_command(app, "OCR");
                scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneResponse);
                consumed = true;
                break;
                
            case ESP32_AI_EventCount:
                esp32_ai_app_send_command(app, "COUNT");
                scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneResponse);
                consumed = true;
                break;
                
            case ESP32_AI_EventFlashToggle:
                esp32_ai_app_send_command(app, "FLASH_TOGGLE");
                app->flash_state = !app->flash_state;
                notification_message(app->notifications, &sequence_blink_cyan_100);
                consumed = true;
                break;
                
            case ESP32_AI_EventPTTStart:
                scene_manager_next_scene(app->scene_manager, ESP32_AI_ScenePTTRecord);
                consumed = true;
                break;
                
            case ESP32_AI_EventStatus:
                esp32_ai_app_send_command(app, "STATUS");
                scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneResponse);
                consumed = true;
                break;
        }
    }
    
    return consumed;
}

void esp32_ai_scene_main_on_exit(void* context) {
    UNUSED(context);
}

// Scene: PTT Recording
void esp32_ai_scene_ptt_record_on_enter(void* context) {
    ESP32_AI_App* app = context;
    DialogEx* dialog = app->dialog_ex;
    
    dialog_ex_set_header(dialog, "Voice Recording", 64, 10, AlignCenter, AlignTop);
    dialog_ex_set_text(dialog, "🎤 RECORDING...\n\nHold OK to record\nRelease to process\n\nPress Back to cancel", 64, 32, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(dialog, "Back");
    dialog_ex_set_center_button_text(dialog, "Hold to Record");
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32_AI_ViewDialog);
    
    // Start recording
    esp32_ai_app_send_command(app, "PTT_START");
    app->ptt_active = true;
    
    // LED feedback
    notification_message(app->notifications, &sequence_blink_red_100);
}

bool esp32_ai_scene_ptt_record_on_event(void* context, SceneManagerEvent event) {
    ESP32_AI_App* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ESP32_AI_EventBack) {
            if(app->ptt_active) {
                esp32_ai_app_send_command(app, "PTT_STOP");
                app->ptt_active = false;
            }
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(app->ptt_active) {
            esp32_ai_app_send_command(app, "PTT_STOP");
            app->ptt_active = false;
        }
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

void esp32_ai_scene_ptt_record_on_exit(void* context) {
    ESP32_AI_App* app = context;
    if(app->ptt_active) {
        esp32_ai_app_send_command(app, "PTT_STOP");
        app->ptt_active = false;
    }
}

// Scene: Response Display
void esp32_ai_scene_response_on_enter(void* context) {
    ESP32_AI_App* app = context;
    TextBox* text_box = app->text_box;
    
    text_box_reset(text_box);
    text_box_set_text(text_box, "⏳ Waiting for ESP32 response...");
    text_box_set_focus(text_box, TextBoxFocusStart);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32_AI_ViewTextBox);
    
    // Wait for response in background
    if(esp32_ai_app_wait_response(app, RESPONSE_TIMEOUT)) {
        text_box_set_text(text_box, furi_string_get_cstr(app->response_text));
        notification_message(app->notifications, &sequence_success);
    } else {
        text_box_set_text(text_box, "❌ No response from ESP32\nCheck connections:\n- Pin 13 (TX) → ESP32 RX\n- Pin 14 (RX) → ESP32 TX\n- Pin 8/18 (GND) → ESP32 GND\n- Pin 1 (5V) → ESP32 5V");
        notification_message(app->notifications, &sequence_error);
    }
}

bool esp32_ai_scene_response_on_event(void* context, SceneManagerEvent event) {
    ESP32_AI_App* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

void esp32_ai_scene_response_on_exit(void* context) {
    UNUSED(context);
}

// Scene handlers array
void (*const esp32_ai_scene_on_enter_handlers[])(void*) = {
    esp32_ai_scene_main_on_enter,
    esp32_ai_scene_response_on_enter,
    esp32_ai_scene_ptt_record_on_enter,
};

bool (*const esp32_ai_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    esp32_ai_scene_main_on_event,
    esp32_ai_scene_response_on_event,
    esp32_ai_scene_ptt_record_on_event,
};

void (*const esp32_ai_scene_on_exit_handlers[])(void*) = {
    esp32_ai_scene_main_on_exit,
    esp32_ai_scene_response_on_exit,
    esp32_ai_scene_ptt_record_on_exit,
};

// Scene manager config
const SceneManagerHandlers esp32_ai_scene_handlers = {
    .on_enter_handlers = esp32_ai_scene_on_enter_handlers,
    .on_event_handlers = esp32_ai_scene_on_event_handlers,
    .on_exit_handlers = esp32_ai_scene_on_exit_handlers,
    .scene_num = ESP32_AI_SceneCount_,
};

// View dispatcher callbacks
bool esp32_ai_view_dispatcher_navigation_event_callback(void* context) {
    ESP32_AI_App* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

bool esp32_ai_view_dispatcher_custom_event_callback(void* context, uint32_t event) {
    ESP32_AI_App* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

// Submenu callback
void esp32_ai_submenu_callback(void* context, uint32_t index) {
    ESP32_AI_App* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

// Dialog callback
void esp32_ai_dialog_callback(DialogExResult result, void* context) {
    ESP32_AI_App* app = context;
    
    if(result == DialogExResultLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ESP32_AI_EventBack);
    } else if(result == DialogExResultCenter) {
        // PTT Stop on button release
        if(app->ptt_active) {
            esp32_ai_app_send_command(app, "PTT_STOP");
            app->ptt_active = false;
            scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneResponse);
        }
    }
}

// App allocation
static ESP32_AI_App* esp32_ai_app_alloc() {
    ESP32_AI_App* app = malloc(sizeof(ESP32_AI_App));
    
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&esp32_ai_scene_handlers, app);
    
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->dialog_ex = dialog_ex_alloc();
    
    // View dispatcher setup
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, esp32_ai_view_dispatcher_navigation_event_callback);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, esp32_ai_view_dispatcher_custom_event_callback);
    
    // Add views
    view_dispatcher_add_view(app->view_dispatcher, ESP32_AI_ViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, ESP32_AI_ViewTextBox, text_box_get_view(app->text_box));
    view_dispatcher_add_view(app->view_dispatcher, ESP32_AI_ViewDialog, dialog_ex_get_view(app->dialog_ex));
    
    // Set callbacks
    submenu_set_callback(app->submenu, esp32_ai_submenu_callback, app);
    dialog_ex_set_callback(app->dialog_ex, esp32_ai_dialog_callback, app);
    
    // Initialize strings
    app->response_text = furi_string_alloc();
    app->status_text = furi_string_alloc();
    
    // Initialize state
    app->is_connected = false;
    app->ptt_active = false;
    app->flash_state = false;
    
    return app;
}

// App deallocation
static void esp32_ai_app_free(ESP32_AI_App* app) {
    esp32_ai_app_uart_deinit(app);
    
    view_dispatcher_remove_view(app->view_dispatcher, ESP32_AI_ViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, ESP32_AI_ViewTextBox);
    view_dispatcher_remove_view(app->view_dispatcher, ESP32_AI_ViewDialog);
    
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    dialog_ex_free(app->dialog_ex);
    
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_string_free(app->response_text);
    furi_string_free(app->status_text);
    
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

// Main entry point
int32_t esp32_ai_monitor_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "ESP32-CAM AI Monitor starting...");
    
    ESP32_AI_App* app = esp32_ai_app_alloc();
    
    // Initialize UART communication
    esp32_ai_app_uart_init(app);
    
    // Attach to GUI
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    // Start with main scene
    scene_manager_next_scene(app->scene_manager, ESP32_AI_SceneMain);
    
    // Run app
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    esp32_ai_app_free(app);
    
    FURI_LOG_I(TAG, "ESP32-CAM AI Monitor finished");
    
    return 0;
}
