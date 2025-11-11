#include "display_manager.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

static const char* TAG = "DisplayManager";

// Hardware pin definitions for ESP32-C6 Touch LCD 1.69"
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)
#define LCD_H_RES           240
#define LCD_V_RES           280

#define PIN_NUM_MOSI        2
#define PIN_NUM_CLK         1
#define PIN_NUM_CS          5
#define PIN_NUM_DC          3
#define PIN_NUM_RST         4
#define PIN_NUM_BL          6

// Touch pins
#define PIN_NUM_TOUCH_SDA   8
#define PIN_NUM_TOUCH_SCL   7
#define PIN_NUM_TOUCH_INT   11
#define PIN_NUM_TOUCH_RST   (-1)

DisplayManager::DisplayManager()
    : io_handle_(nullptr)
    , panel_handle_(nullptr)
    , touch_handle_(nullptr)
    , lvgl_disp_(nullptr)
    , lvgl_touch_(nullptr)
    , label_(nullptr)
    , idle_container_(nullptr)
    , button_(nullptr)
    , button_label_(nullptr)
    , button_callback_(nullptr)
    , initialized_(false)
    , brightness_(100)
{
}

DisplayManager::~DisplayManager() {
    deinit();
}

bool DisplayManager::init() {
    if (initialized_) {
        ESP_LOGW(TAG, "Display already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing display...");

    // Initialize LVGL library
    initLVGL();
    
    // Initialize display hardware
    initDisplay();
    
    // Initialize touch input
    initTouch();
    
    // Initialize backlight
    initBacklight();

    // Create UI elements
    lockLVGL();
    
    // Set black background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
    
    // Create label for text (hidden by default)
    label_ = lv_label_create(lv_scr_act());
    lv_obj_set_width(label_, LCD_H_RES - 20);
    lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    
    // Create idle container (hidden by default)
    idle_container_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(idle_container_, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(idle_container_, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(idle_container_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(idle_container_, 0, LV_PART_MAIN);
    lv_obj_align(idle_container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(idle_container_, LV_OBJ_FLAG_HIDDEN);
    
    unlockLVGL();

    initialized_ = true;
    ESP_LOGI(TAG, "Display initialized successfully");
    
    return true;
}

void DisplayManager::deinit() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing display...");

    lockLVGL();
    if (label_) {
        lv_obj_del(label_);
        label_ = nullptr;
    }
    if (idle_container_) {
        lv_obj_del(idle_container_);
        idle_container_ = nullptr;
    }
    unlockLVGL();

    if (lvgl_touch_) {
        lvgl_port_remove_touch(lvgl_touch_);
        lvgl_touch_ = nullptr;
    }

    if (lvgl_disp_) {
        lvgl_port_remove_disp(lvgl_disp_);
        lvgl_disp_ = nullptr;
    }

    if (touch_handle_) {
        esp_lcd_touch_del(touch_handle_);
        touch_handle_ = nullptr;
    }

    if (panel_handle_) {
        esp_lcd_panel_del(panel_handle_);
        panel_handle_ = nullptr;
    }

    if (io_handle_) {
        esp_lcd_panel_io_del(io_handle_);
        io_handle_ = nullptr;
    }

    spi_bus_free(LCD_HOST);
    lvgl_port_deinit();

    initialized_ = false;
    ESP_LOGI(TAG, "Display deinitialized");
}

void DisplayManager::initLVGL() {
    ESP_LOGI(TAG, "Initializing LVGL...");
    
    lv_init();
    
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed: %s", esp_err_to_name(ret));
    }
}

void DisplayManager::initDisplay() {
    ESP_LOGI(TAG, "Initializing SPI bus...");
    
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = PIN_NUM_CLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t);
    
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Installing panel IO...");
    
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = PIN_NUM_CS;
    io_config.dc_gpio_num = PIN_NUM_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle_));

    ESP_LOGI(TAG, "Installing ST7789 panel driver...");
    
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_NUM_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle_, &panel_config, &panel_handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle_, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle_, true));

    ESP_LOGI(TAG, "Initializing LVGL display driver...");
    
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = io_handle_;
    disp_cfg.panel_handle = panel_handle_;
    disp_cfg.buffer_size = LCD_H_RES * 40;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = LCD_H_RES;
    disp_cfg.vres = LCD_V_RES;
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.sw_rotate = false;
    disp_cfg.flags.swap_bytes = true;

    lvgl_disp_ = lvgl_port_add_disp(&disp_cfg);
    lv_disp_set_default(lvgl_disp_);
    
    // Set gap after adding display
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle_, 0, 20));
    
    ESP_LOGI(TAG, "Display initialized: %dx%d", LCD_H_RES, LCD_V_RES);
}

void DisplayManager::initTouch() {
    ESP_LOGI(TAG, "Initializing I2C for touch...");
    
    // Initialize I2C bus
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_bus_config_t i2c_bus_config = {};
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port = I2C_NUM_0;
    i2c_bus_config.scl_io_num = (gpio_num_t)PIN_NUM_TOUCH_SCL;
    i2c_bus_config.sda_io_num = (gpio_num_t)PIN_NUM_TOUCH_SDA;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Initializing touch panel IO...");
    
    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t touch_io_config = {};
    touch_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    touch_io_config.control_phase_bytes = 1;
    touch_io_config.dc_bit_offset = 0;
    touch_io_config.lcd_cmd_bits = 8;
    touch_io_config.lcd_param_bits = 8;
    touch_io_config.flags.disable_control_phase = 1;
    touch_io_config.scl_speed_hz = 400000;  // 400kHz
    
    ret = esp_lcd_new_panel_io_i2c(i2c_bus_handle, &touch_io_config, &touch_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch panel IO: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Initializing CST816S touch controller...");
    
    esp_lcd_touch_config_t touch_config = {};
    touch_config.x_max = LCD_H_RES;
    touch_config.y_max = LCD_V_RES;
    touch_config.rst_gpio_num = (gpio_num_t)PIN_NUM_TOUCH_RST;
    touch_config.int_gpio_num = (gpio_num_t)PIN_NUM_TOUCH_INT;
    touch_config.flags.swap_xy = false;
    touch_config.flags.mirror_x = false;
    touch_config.flags.mirror_y = false;
    
    ret = esp_lcd_touch_new_i2c_cst816s(touch_io_handle, &touch_config, &touch_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch handle: %s", esp_err_to_name(ret));
        return;
    }

    // Register touch with LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp_,
        .handle = touch_handle_,
        .scale = {1.0f, 1.0f}
    };
    
    lvgl_touch_ = lvgl_port_add_touch(&touch_cfg);
    
    ESP_LOGI(TAG, "Touch initialized successfully");
}

void DisplayManager::initBacklight() {
    ESP_LOGI(TAG, "Initializing backlight...");
    
    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.pin_bit_mask = 1ULL << PIN_NUM_BL;
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;
    bk_gpio_config.pull_up_en = GPIO_PULLUP_DISABLE;
    bk_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bk_gpio_config.intr_type = GPIO_INTR_DISABLE;
    
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level((gpio_num_t)PIN_NUM_BL, 1);
    
    ESP_LOGI(TAG, "Backlight enabled");
}

void DisplayManager::lockLVGL() {
    lvgl_port_lock(0);
}

void DisplayManager::unlockLVGL() {
    lvgl_port_unlock();
}

void DisplayManager::buttonEventCallback(lv_event_t* e) {
    DisplayManager* self = static_cast<DisplayManager*>(lv_event_get_user_data(e));
    if (self && self->button_callback_) {
        ESP_LOGI(TAG, "Button clicked!");
        self->button_callback_();
    }
}

void DisplayManager::showIdleScreen(std::function<void()> callback) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    button_callback_ = callback;

    lockLVGL();
    
    // Hide other UI elements
    if (label_) {
        lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Clear and show idle container
    lv_obj_clean(idle_container_);
    lv_obj_clear_flag(idle_container_, LV_OBJ_FLAG_HIDDEN);
    
    // Create title label
    lv_obj_t* title_label = lv_label_create(idle_container_);
    lv_label_set_text(title_label, "X402 Client");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);
    
    // Create subtitle
    lv_obj_t* subtitle_label = lv_label_create(idle_container_);
    lv_label_set_text(subtitle_label, "Ready");
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 55);
    
    // Create payment button
    button_ = lv_btn_create(idle_container_);
    lv_obj_set_size(button_, 160, 50);
    lv_obj_align(button_, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(button_, lv_color_hex(0x2196F3), LV_PART_MAIN);
    lv_obj_set_style_radius(button_, 8, LV_PART_MAIN);
    
    // Button label
    button_label_ = lv_label_create(button_);
    lv_label_set_text(button_label_, "Start Payment");
    lv_obj_set_style_text_color(button_label_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(button_label_);
    
    // Register button click event
    lv_obj_add_event_cb(button_, buttonEventCallback, LV_EVENT_CLICKED, this);
    
    // Add instruction text
    lv_obj_t* instruction_label = lv_label_create(idle_container_);
    lv_label_set_text(instruction_label, "Tap button to begin");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align(instruction_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    unlockLVGL();
    
    ESP_LOGI(TAG, "Idle screen displayed");
}

void DisplayManager::showText(const char* text, bool centered) {
    if (!initialized_ || !label_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    lockLVGL();
    
    // Hide idle screen
    if (idle_container_) {
        lv_obj_add_flag(idle_container_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show and update label
    lv_obj_clear_flag(label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_text(label_, text);
    
    if (centered) {
        lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label_, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(label_, LV_ALIGN_TOP_LEFT, 10, 10);
    }
    
    unlockLVGL();
}

void DisplayManager::showStatus(const char* title, const char* message) {
    if (!initialized_ || !label_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    char buffer[256];
    if (message) {
        snprintf(buffer, sizeof(buffer), "%s\n\n%s", title, message);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", title);
    }

    showText(buffer, true);
}

void DisplayManager::showSuccess(const char* message) {
    if (!initialized_ || !label_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    lockLVGL();
    
    // Hide idle screen
    if (idle_container_) {
        lv_obj_add_flag(idle_container_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show label with green text
    lv_obj_clear_flag(label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(label_, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_label_set_text(label_, message);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, 0);
    
    unlockLVGL();
}

void DisplayManager::showError(const char* message) {
    if (!initialized_ || !label_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    lockLVGL();
    
    // Hide idle screen
    if (idle_container_) {
        lv_obj_add_flag(idle_container_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show label with red text
    lv_obj_clear_flag(label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(label_, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_label_set_text(label_, message);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, 0);
    
    unlockLVGL();
}

void DisplayManager::clear() {
    if (!initialized_ || !label_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    lockLVGL();
    lv_label_set_text(label_, "");
    lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    unlockLVGL();
}

void DisplayManager::hideAll() {
    if (!initialized_) {
        ESP_LOGW(TAG, "Display not initialized");
        return;
    }

    lockLVGL();
    
    if (label_) {
        lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (idle_container_) {
        lv_obj_add_flag(idle_container_, LV_OBJ_FLAG_HIDDEN);
    }
    
    unlockLVGL();
}

void DisplayManager::setBrightness(uint8_t brightness) {
    if (brightness > 100) {
        brightness = 100;
    }
    
    brightness_ = brightness;
    
    // Simple on/off control (can be enhanced with PWM)
    if (brightness > 0) {
        gpio_set_level((gpio_num_t)PIN_NUM_BL, 1);
    } else {
        gpio_set_level((gpio_num_t)PIN_NUM_BL, 0);
    }
}