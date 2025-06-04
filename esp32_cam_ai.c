#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/popup.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <expansion/expansion.h>

#define TAG "ESP32CamAI"

// UART Configuration matching ESP32-CAM firmware
#define UART_CH (FuriHalSerialIdUsart)
#define BAUDRATE (115200)

// Application scenes
typedef enum {
    ESP32CamAISceneStart,
    ESP32CamAISceneMenu,
    ESP32CamAISceneResponse,
    ESP32CamAIScenePTT,
    ESP32CamAISceneSettings,
    ESP32CamAISceneCustomVision,     // NUOVO
    ESP32CamAISceneCustomChat,       // NUOVO
    ESP32CamAISceneTextInput,        // NUOVO
    ESP32CamAISceneCount,
} ESP32CamAIScene;

// Application views  
typedef enum {
    ESP32CamAIViewSubmenu,
    ESP32CamAIViewResponse,
    ESP32CamAIViewPTT,
    ESP32CamAIViewSettings,
    ESP32CamAIViewTextInput,         // NUOVO
} ESP32CamAIView;

// Application events
typedef enum {
    ESP32CamAIEventStartPressed,
    ESP32CamAIEventVisionPressed,
    ESP32CamAIEventMathPressed,
    ESP32CamAIEventOCRPressed,
    ESP32CamAIEventCountPressed,
    ESP32CamAIEventPTTPressed,
    ESP32CamAIEventFlashOnPressed,
    ESP32CamAIEventFlashOffPressed,
    ESP32CamAIEventFlashTogglePressed,
    ESP32CamAIEventStatusPressed,
    ESP32CamAIEventSettingsPressed,
    ESP32CamAIEventCustomVisionPressed,  // NUOVO
    ESP32CamAIEventCustomChatPressed,    // NUOVO
    ESP32CamAIEventTextInputDone,        // NUOVO
    ESP32CamAIEventBack,
    ESP32CamAIEventUpdateResponse,
} ESP32CamAIEvent;

// Main application structure
typedef struct ESP32CamAI ESP32CamAI;

struct ESP32CamAI {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    
    // Views
    Submenu* submenu;
    TextBox* text_box_response;
    Popup* popup_ptt;
    VariableItemList* variable_item_list;
    TextInput* text_input;              // NUOVO
    
    // UART
    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker_thread;
    FuriTimer* response_timer;
    
    // Notifications
    NotificationApp* notifications;
    
    // Data
    FuriString* response_text;
    FuriString* line_buffer;
    char input_buffer[128];             // CORRETTO: buffer char array
    bool uart_connected;
    bool ptt_active;
    bool flash_status;
    bool response_updated;
    bool is_vision_mode;                // NUOVO: distingue vision/chat
    
    // Navigation state
    uint32_t current_scene;
    
    // Settings
    uint32_t baudrate;
};

// Function declarations
static void esp32_cam_ai_scene_start_callback(void* context, uint32_t index);
static void esp32_cam_ai_scene_menu_callback(void* context, uint32_t index);
static void esp32_cam_ai_text_input_callback(void* context);  // NUOVO
static bool esp32_cam_ai_navigation_exit_callback(void* context);

// UART Functions
static void esp32_cam_ai_uart_send_command(ESP32CamAI* app, const char* command) {
    if(app->serial_handle) {
        furi_hal_serial_tx(app->serial_handle, (const uint8_t*)command, strlen(command));
        furi_hal_serial_tx(app->serial_handle, (const uint8_t*)"\n", 1);
        FURI_LOG_I(TAG, "Sent command: %s", command);
        
        // Show command sent immediately
        furi_string_printf(app->response_text, "📤 Sent: %s\nWaiting for response...", command);
        app->response_updated = true;
    }
}

// NUOVO: Invio comando custom con domanda
static void esp32_cam_ai_uart_send_custom_command(ESP32CamAI* app, const char* prefix, const char* question) {
    if(app->serial_handle) {
        // Costruisci comando: "CUSTOM_VISION:domanda" o "CUSTOM_CHAT:domanda"
        FuriString* full_command = furi_string_alloc();
        furi_string_printf(full_command, "%s%s", prefix, question);
        
        const char* cmd_str = furi_string_get_cstr(full_command);
        furi_hal_serial_tx(app->serial_handle, (const uint8_t*)cmd_str, strlen(cmd_str));
        furi_hal_serial_tx(app->serial_handle, (const uint8_t*)"\n", 1);
        
        FURI_LOG_I(TAG, "Sent custom command: %s", cmd_str);
        
        furi_string_printf(app->response_text, "📤 Question: %s\nProcessing...", question);
        app->response_updated = true;
        
        furi_string_free(full_command);
    }
}

// Response timer callback to refresh UI
static void esp32_cam_ai_response_timer_callback(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    if(app->response_updated) {
        // Send custom event to update response view
        view_dispatcher_send_custom_event(app->view_dispatcher, ESP32CamAIEventUpdateResponse);
        app->response_updated = false;
    }
}

static void esp32_cam_ai_uart_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    UNUSED(handle);
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    if(event == FuriHalSerialRxEventData) {
        uint8_t data;
        data = furi_hal_serial_async_rx(app->serial_handle);
        furi_stream_buffer_send(app->rx_stream, &data, 1, 0);
    }
}

static int32_t esp32_cam_ai_worker(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    uint8_t data;
    
    FURI_LOG_I(TAG, "Worker thread started");
    
    while(1) {
        // Read data from stream buffer
        size_t ret = furi_stream_buffer_receive(app->rx_stream, &data, 1, 100);
        if(ret > 0) {
            // Add byte to line buffer
            if(data == '\n' || data == '\r') {
                if(furi_string_size(app->line_buffer) > 0) {
                    // Process complete line
                    const char* line = furi_string_get_cstr(app->line_buffer);
                    
                    FURI_LOG_I(TAG, "Received line: '%s'", line);
                    
                    // Process different responses
                    if(strstr(line, "READY")) {
                        furi_string_set(app->response_text, "✅ ESP32-CAM Ready");
                        app->uart_connected = true;
                    }
                    else if(strstr(line, "RECORDING")) {
                        furi_string_set(app->response_text, "🎤 Recording audio...");
                        app->ptt_active = true;
                    }
                    else if(strstr(line, "PROCESSING")) {
                        furi_string_set(app->response_text, "⚙️ Processing voice...");
                    }
                    else if(strstr(line, "FLASH:ON")) {
                        furi_string_set(app->response_text, "💡 Flash LED ON");
                        app->flash_status = true;
                    }
                    else if(strstr(line, "FLASH:OFF")) {
                        furi_string_set(app->response_text, "🔲 Flash LED OFF");
                        app->flash_status = false;
                    }
                    else if(strstr(line, "OK:")) {
                        const char* response = line + 3;
                        furi_string_printf(app->response_text, "✅ %s", response);
                        app->ptt_active = false;
                    }
                    else if(strstr(line, "ERROR:")) {
                        const char* error = line + 6;
                        furi_string_printf(app->response_text, "❌ %s", error);
                        app->ptt_active = false;
                    }
                    else if(strstr(line, "VOICE_RECOGNIZED:")) {
                        const char* voice_text = line + 17;
                        furi_string_printf(app->response_text, "🗣️ '%s'", voice_text);
                    }
                    else if(strstr(line, "STATUS:")) {
                        const char* status = line + 7;
                        furi_string_printf(app->response_text, "ℹ️ %s", status);
                    }
                    else if(strlen(line) > 2) {
                        // Any other response
                        furi_string_printf(app->response_text, "📥 %s", line);
                    }
                    
                    // Mark response as updated
                    app->response_updated = true;
                    
                    // Clear line buffer
                    furi_string_reset(app->line_buffer);
                }
            } else if(furi_string_size(app->line_buffer) < 512) {
                // Add character to line buffer
                furi_string_push_back(app->line_buffer, data);
            }
        }
        
        // Check if thread should exit
        if(furi_thread_flags_get() & (1UL << 0)) {
            break;
        }
    }
    
    FURI_LOG_I(TAG, "Worker thread stopped");
    return 0;
}

static bool esp32_cam_ai_uart_init(ESP32CamAI* app) {
    FURI_LOG_I(TAG, "Initializing UART...");
    
    app->serial_handle = furi_hal_serial_control_acquire(UART_CH);
    if(!app->serial_handle) {
        FURI_LOG_E(TAG, "Failed to acquire serial handle");
        return false;
    }
    
    furi_hal_serial_init(app->serial_handle, app->baudrate);
    furi_hal_serial_async_rx_start(app->serial_handle, esp32_cam_ai_uart_rx_callback, app, false);
    
    app->rx_stream = furi_stream_buffer_alloc(1024, 1);
    
    app->worker_thread = furi_thread_alloc_ex("ESP32CamWorker", 1024, esp32_cam_ai_worker, app);
    furi_thread_start(app->worker_thread);
    
    // Start response timer for UI updates
    app->response_timer = furi_timer_alloc(esp32_cam_ai_response_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->response_timer, 250); // Update every 250ms
    
    FURI_LOG_I(TAG, "UART initialized at %lu baud", app->baudrate);
    
    // Send initial STATUS command
    furi_delay_ms(100);
    esp32_cam_ai_uart_send_command(app, "STATUS");
    
    return true;
}

static void esp32_cam_ai_uart_deinit(ESP32CamAI* app) {
    if(app->response_timer) {
        furi_timer_stop(app->response_timer);
        furi_timer_free(app->response_timer);
        app->response_timer = NULL;
    }
    
    if(app->worker_thread) {
        furi_thread_flags_set(furi_thread_get_id(app->worker_thread), (1UL << 0));
        furi_thread_join(app->worker_thread);
        furi_thread_free(app->worker_thread);
        app->worker_thread = NULL;
    }
    
    if(app->rx_stream) {
        furi_stream_buffer_free(app->rx_stream);
        app->rx_stream = NULL;
    }
    
    if(app->serial_handle) {
        furi_hal_serial_async_rx_stop(app->serial_handle);
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
        app->serial_handle = NULL;
    }
}

// Scene: Start
static void esp32_cam_ai_scene_start_on_enter(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "ESP32-CAM AI Vision");
    
    submenu_add_item(
        app->submenu, 
        "Connect & Start", 
        ESP32CamAIEventStartPressed, 
        esp32_cam_ai_scene_start_callback, 
        app
    );
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32CamAIViewSubmenu);
}

static bool esp32_cam_ai_scene_start_on_event(void* context, SceneManagerEvent event) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
            case ESP32CamAIEventStartPressed:
                if(esp32_cam_ai_uart_init(app)) {
                    scene_manager_next_scene(app->scene_manager, ESP32CamAISceneMenu);
                } else {
                    furi_string_set(app->response_text, "❌ UART Init Failed");
                    scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                }
                consumed = true;
                break;
        }
    }
    
    return consumed;
}

static void esp32_cam_ai_scene_start_on_exit(void* context) {
    UNUSED(context);
}

static void esp32_cam_ai_scene_start_callback(void* context, uint32_t index) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    scene_manager_handle_custom_event(app->scene_manager, index);
}

// Scene: Menu
static void esp32_cam_ai_scene_menu_on_enter(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "ESP32-CAM Commands");
    
    // AI Vision Commands
    submenu_add_item(app->submenu, "📷 Vision Analysis", ESP32CamAIEventVisionPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "🧮 Math Solver", ESP32CamAIEventMathPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "📝 Text OCR", ESP32CamAIEventOCRPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "🔢 Count Objects", ESP32CamAIEventCountPressed, esp32_cam_ai_scene_menu_callback, app);
    
    // NUOVO: Custom Questions
    submenu_add_item(app->submenu, "❓ Custom Vision", ESP32CamAIEventCustomVisionPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "💬 Chat Question", ESP32CamAIEventCustomChatPressed, esp32_cam_ai_scene_menu_callback, app);
    
    // Voice Command
    submenu_add_item(app->submenu, "🎤 Voice Command (PTT)", ESP32CamAIEventPTTPressed, esp32_cam_ai_scene_menu_callback, app);
    
    // Flash Controls
    submenu_add_item(app->submenu, "💡 Flash ON", ESP32CamAIEventFlashOnPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "🔲 Flash OFF", ESP32CamAIEventFlashOffPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "🔄 Flash Toggle", ESP32CamAIEventFlashTogglePressed, esp32_cam_ai_scene_menu_callback, app);
    
    // System
    submenu_add_item(app->submenu, "ℹ️ System Status", ESP32CamAIEventStatusPressed, esp32_cam_ai_scene_menu_callback, app);
    submenu_add_item(app->submenu, "⚙️ Settings", ESP32CamAIEventSettingsPressed, esp32_cam_ai_scene_menu_callback, app);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32CamAIViewSubmenu);
}

static bool esp32_cam_ai_scene_menu_on_event(void* context, SceneManagerEvent event) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
            case ESP32CamAIEventVisionPressed:
                esp32_cam_ai_uart_send_command(app, "VISION");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventMathPressed:
                esp32_cam_ai_uart_send_command(app, "MATH");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventOCRPressed:
                esp32_cam_ai_uart_send_command(app, "OCR");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventCountPressed:
                esp32_cam_ai_uart_send_command(app, "COUNT");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            // NUOVO: Custom Questions
            case ESP32CamAIEventCustomVisionPressed:
                app->is_vision_mode = true;
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneTextInput);
                consumed = true;
                break;
                
            case ESP32CamAIEventCustomChatPressed:
                app->is_vision_mode = false;
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneTextInput);
                consumed = true;
                break;
                
            case ESP32CamAIEventPTTPressed:
                scene_manager_next_scene(app->scene_manager, ESP32CamAIScenePTT);
                consumed = true;
                break;
                
            case ESP32CamAIEventFlashOnPressed:
                esp32_cam_ai_uart_send_command(app, "FLASH_ON");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventFlashOffPressed:
                esp32_cam_ai_uart_send_command(app, "FLASH_OFF");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventFlashTogglePressed:
                esp32_cam_ai_uart_send_command(app, "FLASH_TOGGLE");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventStatusPressed:
                esp32_cam_ai_uart_send_command(app, "STATUS");
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                consumed = true;
                break;
                
            case ESP32CamAIEventSettingsPressed:
                scene_manager_next_scene(app->scene_manager, ESP32CamAISceneSettings);
                consumed = true;
                break;
        }
    }
    
    // Handle back button press - go to start scene
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

static void esp32_cam_ai_scene_menu_on_exit(void* context) {
    UNUSED(context);
}

static void esp32_cam_ai_scene_menu_callback(void* context, uint32_t index) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    scene_manager_handle_custom_event(app->scene_manager, index);
}

// NUOVO: Scene Text Input
static void esp32_cam_ai_scene_text_input_on_enter(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    // Reset input buffer
    memset(app->input_buffer, 0, sizeof(app->input_buffer));
    
    // Setup text input
    text_input_reset(app->text_input);
    
    if(app->is_vision_mode) {
        text_input_set_header_text(app->text_input, "Custom Vision Question:");
    } else {
        text_input_set_header_text(app->text_input, "Chat Question:");
    }
    
    text_input_set_result_callback(
        app->text_input,
        esp32_cam_ai_text_input_callback,
        app,
        app->input_buffer,
        sizeof(app->input_buffer),
        true // clear default text
    );
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32CamAIViewTextInput);
}

static bool esp32_cam_ai_scene_text_input_on_event(void* context, SceneManagerEvent event) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
            case ESP32CamAIEventTextInputDone:
                // Input completed - send question
                if(strlen(app->input_buffer) > 0) {
                    if(app->is_vision_mode) {
                        esp32_cam_ai_uart_send_custom_command(app, "CUSTOM_VISION:", app->input_buffer);
                    } else {
                        esp32_cam_ai_uart_send_custom_command(app, "CUSTOM_CHAT:", app->input_buffer);
                    }
                    
                    scene_manager_next_scene(app->scene_manager, ESP32CamAISceneResponse);
                } else {
                    // Empty input - go back to menu
                    scene_manager_previous_scene(app->scene_manager);
                }
                consumed = true;
                break;
        }
    }
    
    // Handle back button press
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

static void esp32_cam_ai_scene_text_input_on_exit(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    text_input_reset(app->text_input);
}

// NUOVO: Text Input Callback
static void esp32_cam_ai_text_input_callback(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    scene_manager_handle_custom_event(app->scene_manager, ESP32CamAIEventTextInputDone);
}

// Scene: Response Display
static void esp32_cam_ai_scene_response_on_enter(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    text_box_reset(app->text_box_response);
    text_box_set_text(app->text_box_response, furi_string_get_cstr(app->response_text));
    text_box_set_focus(app->text_box_response, TextBoxFocusStart);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32CamAIViewResponse);
}

static bool esp32_cam_ai_scene_response_on_event(void* context, SceneManagerEvent event) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
            case ESP32CamAIEventBack:
                scene_manager_previous_scene(app->scene_manager);
                consumed = true;
                break;
                
            case ESP32CamAIEventUpdateResponse:
                // Update the text box with new response
                text_box_reset(app->text_box_response);
                text_box_set_text(app->text_box_response, furi_string_get_cstr(app->response_text));
                text_box_set_focus(app->text_box_response, TextBoxFocusStart);
                consumed = true;
                break;
        }
    }
    
    // Handle back button press
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

static void esp32_cam_ai_scene_response_on_exit(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    text_box_reset(app->text_box_response);
}

// Scene: PTT (Push-to-Talk)
static void esp32_cam_ai_scene_ptt_on_enter(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    popup_reset(app->popup_ptt);
    
    if(app->ptt_active) {
        popup_set_header(app->popup_ptt, "🎤 RECORDING", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(app->popup_ptt, "Release OK to stop", 64, 35, AlignCenter, AlignCenter);
    } else {
        popup_set_header(app->popup_ptt, "🎤 Push-to-Talk", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(app->popup_ptt, "Hold OK to record\nBack to cancel", 64, 35, AlignCenter, AlignCenter);
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32CamAIViewPTT);
}

static bool esp32_cam_ai_scene_ptt_on_event(void* context, SceneManagerEvent event) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ESP32CamAIEventBack) {
            if(app->ptt_active) {
                esp32_cam_ai_uart_send_command(app, "PTT_STOP");
                app->ptt_active = false;
            }
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    
    // Handle PTT input events within scene
    if(event.type == SceneManagerEventTypeBack) {
        // Back button pressed
        if(app->ptt_active) {
            esp32_cam_ai_uart_send_command(app, "PTT_STOP");
            app->ptt_active = false;
        }
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

static void esp32_cam_ai_scene_ptt_on_exit(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    popup_reset(app->popup_ptt);
}

// Scene: Settings
static void esp32_cam_ai_scene_settings_on_enter(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    variable_item_list_reset(app->variable_item_list);
    
    VariableItem* item = variable_item_list_add(
        app->variable_item_list,
        "Baudrate",
        1,
        NULL,
        NULL
    );
    
    char baudrate_text[32];
    snprintf(baudrate_text, sizeof(baudrate_text), "%lu", app->baudrate);
    variable_item_set_current_value_text(item, baudrate_text);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, ESP32CamAIViewSettings);
}

static bool esp32_cam_ai_scene_settings_on_event(void* context, SceneManagerEvent event) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ESP32CamAIEventBack) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    
    // Handle back button press
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    
    return consumed;
}

static void esp32_cam_ai_scene_settings_on_exit(void* context) {
    ESP32CamAI* app = (ESP32CamAI*)context;
    variable_item_list_reset(app->variable_item_list);
}

// Scene handlers table
void (*const esp32_cam_ai_scene_on_enter_handlers[])(void*) = {
    esp32_cam_ai_scene_start_on_enter,
    esp32_cam_ai_scene_menu_on_enter,
    esp32_cam_ai_scene_response_on_enter,
    esp32_cam_ai_scene_ptt_on_enter,
    esp32_cam_ai_scene_settings_on_enter,
    esp32_cam_ai_scene_text_input_on_enter,    // NUOVO
    esp32_cam_ai_scene_text_input_on_enter,    // Custom Chat usa stesso input
    esp32_cam_ai_scene_text_input_on_enter,    // Text Input handler
};

bool (*const esp32_cam_ai_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    esp32_cam_ai_scene_start_on_event,
    esp32_cam_ai_scene_menu_on_event,
    esp32_cam_ai_scene_response_on_event,
    esp32_cam_ai_scene_ptt_on_event,
    esp32_cam_ai_scene_settings_on_event,
    esp32_cam_ai_scene_text_input_on_event,    // NUOVO
    esp32_cam_ai_scene_text_input_on_event,    // Custom Chat
    esp32_cam_ai_scene_text_input_on_event,    // Text Input
};

void (*const esp32_cam_ai_scene_on_exit_handlers[])(void*) = {
    esp32_cam_ai_scene_start_on_exit,
    esp32_cam_ai_scene_menu_on_exit,
    esp32_cam_ai_scene_response_on_exit,
    esp32_cam_ai_scene_ptt_on_exit,
    esp32_cam_ai_scene_settings_on_exit,
    esp32_cam_ai_scene_text_input_on_exit,     // NUOVO
    esp32_cam_ai_scene_text_input_on_exit,     // Custom Chat
    esp32_cam_ai_scene_text_input_on_exit,     // Text Input
};

// Scene manager handlers
static const SceneManagerHandlers esp32_cam_ai_scene_manager_handlers = {
    .on_enter_handlers = esp32_cam_ai_scene_on_enter_handlers,
    .on_event_handlers = esp32_cam_ai_scene_on_event_handlers,
    .on_exit_handlers = esp32_cam_ai_scene_on_exit_handlers,
    .scene_num = ESP32CamAISceneCount,
};

// Navigation callbacks
static bool esp32_cam_ai_navigation_exit_callback(void* context) {
    furi_assert(context);
    ESP32CamAI* app = (ESP32CamAI*)context;
    
    // Check current scene and handle back navigation
    if(app->current_scene == ESP32CamAISceneStart) {
        // Only exit from start scene
        return false; // Allow exit
    } else {
        // For all other scenes, go back to previous scene
        scene_manager_previous_scene(app->scene_manager);
        return true; // Consume event, don't exit
    }
}

// Custom event callback
static bool esp32_cam_ai_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    ESP32CamAI* app = (ESP32CamAI*)context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

// App allocation and initialization
static ESP32CamAI* esp32_cam_ai_app_alloc() {
    ESP32CamAI* app = malloc(sizeof(ESP32CamAI));
    
    // Initialize default values
    app->baudrate = BAUDRATE;
    app->uart_connected = false;
    app->ptt_active = false;
    app->flash_status = false;
    app->response_updated = false;
    app->is_vision_mode = false;  // NUOVO
    
    // Initialize input buffer
    memset(app->input_buffer, 0, sizeof(app->input_buffer));
    
    // GUI
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&esp32_cam_ai_scene_manager_handlers, app);
    
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, esp32_cam_ai_navigation_exit_callback);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, esp32_cam_ai_custom_event_callback);
    
    // Views
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ESP32CamAIViewSubmenu, submenu_get_view(app->submenu));
    
    app->text_box_response = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ESP32CamAIViewResponse, text_box_get_view(app->text_box_response));
    
    app->popup_ptt = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ESP32CamAIViewPTT, popup_get_view(app->popup_ptt));
    
    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ESP32CamAIViewSettings, variable_item_list_get_view(app->variable_item_list));
    
    // NUOVO: Text Input
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ESP32CamAIViewTextInput, text_input_get_view(app->text_input));
    
    // Notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Data
    app->response_text = furi_string_alloc();
    app->line_buffer = furi_string_alloc();
    
    return app;
}

static void esp32_cam_ai_app_free(ESP32CamAI* app) {
    furi_assert(app);
    
    // Deinitialize UART
    esp32_cam_ai_uart_deinit(app);
    
    // Free views
    view_dispatcher_remove_view(app->view_dispatcher, ESP32CamAIViewSubmenu);
    submenu_free(app->submenu);
    
    view_dispatcher_remove_view(app->view_dispatcher, ESP32CamAIViewResponse);
    text_box_free(app->text_box_response);
    
    view_dispatcher_remove_view(app->view_dispatcher, ESP32CamAIViewPTT);
    popup_free(app->popup_ptt);
    
    view_dispatcher_remove_view(app->view_dispatcher, ESP32CamAIViewSettings);
    variable_item_list_free(app->variable_item_list);
    
    // NUOVO: Free text input
    view_dispatcher_remove_view(app->view_dispatcher, ESP32CamAIViewTextInput);
    text_input_free(app->text_input);
    
    // Free GUI
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    
    // Free notifications
    furi_record_close(RECORD_NOTIFICATION);
    
    // Free data
    furi_string_free(app->response_text);
    furi_string_free(app->line_buffer);
    
    free(app);
}

// Main entry point
int32_t esp32_cam_ai_app(void* p) {
    UNUSED(p);
    
    ESP32CamAI* app = esp32_cam_ai_app_alloc();
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, ESP32CamAISceneStart);
    
    view_dispatcher_run(app->view_dispatcher);
    
    esp32_cam_ai_app_free(app);
    
    return 0;
}
