#include "encoder.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/queue.h"

// Pin assignment per docs/DESIGN.md (KY-040 has onboard pull-ups on A/B; SW floats).
#define ENC_PIN_A  GPIO_NUM_3   // CLK
#define ENC_PIN_B  GPIO_NUM_10  // DT
#define ENC_PIN_SW GPIO_NUM_20

#define BTN_FILTER_US 20000  // ignore edges within 20 ms of the last accepted one

static QueueHandle_t s_queue;

// Full-step quadrature decode. State = (A<<1)|B; index = (prev<<2)|curr.
// Each valid Gray-code transition contributes +/-1; a full detent-to-detent
// cycle sums to +/-4, so a step is emitted only back at the detent (11) with a
// complete cycle accumulated. Bounce produces invalid (0) or self-cancelling
// transitions and never reaches +/-4.
static const int8_t QDEC[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0,
};

static void IRAM_ATTR rotation_isr(void* arg) {
    static uint8_t prev = 3;  // detent: both high
    static int8_t accum = 0;

    uint8_t curr = (gpio_get_level(ENC_PIN_A) << 1) | gpio_get_level(ENC_PIN_B);
    if (curr == prev) return;
    accum += QDEC[(prev << 2) | curr];
    prev = curr;

    if (curr == 3) {  // back at detent
        // Sign verified on hardware: a physical CW turn accumulates -4 here.
        EncEvent ev;
        if (accum <= -4) ev = EncEvent::StepCW;
        else if (accum >= 4) ev = EncEvent::StepCCW;
        else { accum = 0; return; }
        accum = 0;
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(s_queue, &ev, &woken);
        if (woken) portYIELD_FROM_ISR();
    }
}

static void IRAM_ATTR button_isr(void* arg) {
    static int64_t last_edge_us = 0;
    static int last_level = 1;  // pulled up = released

    int64_t now = esp_timer_get_time();
    if (now - last_edge_us < BTN_FILTER_US) return;
    int level = gpio_get_level(ENC_PIN_SW);
    if (level == last_level) return;
    last_edge_us = now;
    last_level = level;

    EncEvent ev = level ? EncEvent::BtnUp : EncEvent::BtnDown;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &ev, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void encoder_init() {
    s_queue = xQueueCreate(8, sizeof(EncEvent));

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ENC_PIN_A) | (1ULL << ENC_PIN_B) | (1ULL << ENC_PIN_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // required for SW; harmless backstop for A/B
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENC_PIN_A, rotation_isr, nullptr);
    gpio_isr_handler_add(ENC_PIN_B, rotation_isr, nullptr);
    gpio_isr_handler_add(ENC_PIN_SW, button_isr, nullptr);
}

bool encoder_wait(EncEvent* ev, TickType_t timeout) {
    return xQueueReceive(s_queue, ev, timeout) == pdTRUE;
}
