#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "ws2812.pio.h"

#define IS_RGBW false
#define NUM_PIXELS 25
#define WS2812_PIN 7
#define BUTTON_INCREMENT_PIN 5  // Botão A (incremento)
#define BUTTON_DECREMENT_PIN 6  // Botão B (decremento)
#define LED_RED_PIN 13          // Pino do LED vermelho

// Variáveis globais para armazenar a cor
uint8_t led_r = 0;   // Intensidade do vermelho (ajustada para baixa luminosidade)
uint8_t led_g = 5;  // Intensidade do verde (ajustada para baixa luminosidade)
uint8_t led_b = 5;  // Intensidade do azul (ajustada para baixa luminosidade)

// Variável para armazenar o número atual (0 a 9)
volatile int current_number = 0;

// Buffer para armazenar quais LEDs estão ligados na matriz 5x5
volatile bool led_buffer[NUM_PIXELS] = {0};

// Flags para debounce
volatile bool debounce_increment = false;
volatile bool debounce_decrement = false;

// Tabela de frames para os números de 0 a 9 (matrizes 5x5)
const bool frames[10][NUM_PIXELS] = {
    // Número 0
    {
        1,1,1,1,1,
        1,0,0,0,1,
        1,0,0,0,1,
        1,0,0,0,1,
        1,1,1,1,1
    },
    // Número 1
    {
        0,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0
    },
    // Número 2
    {
        1,1,1,1,1,
        0,0,0,0,1,
        1,1,1,1,1,
        1,0,0,0,0,
        1,1,1,1,1
    },
    // Número 3
    {
        1,1,1,1,1,
        0,0,0,0,1,
        1,1,1,1,1,
        0,0,0,0,1,
        1,1,1,1,1
    },
    // Número 4
    {
        1,0,0,0,1,
        1,0,0,0,1,
        1,1,1,1,1,
        0,0,0,0,1,
        0,0,0,0,1
    },
    // Número 5
    {
        1,1,1,1,1,
        1,0,0,0,0,
        1,1,1,1,1,
        0,0,0,0,1,
        1,1,1,1,1
    },
    // Número 6
    {
        1,1,1,1,1,
        1,0,0,0,0,
        1,1,1,1,1,
        1,0,0,0,1,
        1,1,1,1,1
    },
    // Número 7
    {
        1,1,1,1,1,
        0,0,0,0,1,
        0,0,0,1,0,
        0,0,1,0,0,
        0,1,0,0,0
    },
    // Número 8
    {
        1,1,1,1,1,
        1,0,0,0,1,
        1,1,1,1,1,
        1,0,0,0,1,
        1,1,1,1,1
    },
    // Número 9
    {
        1,1,1,1,1,
        1,0,0,0,1,
        1,1,1,1,1,
        0,0,0,0,1,
        0,0,0,0,1
    }
};

// Função para atualizar o buffer de LEDs com base no número atual
void update_led_buffer(int number) {
    // Mapeamento correto dos LEDs considerando o padrão zigue-zague
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            int index;
            if (row % 2 == 0) {
                // Linhas pares: esquerda para direita
                index = row * 5 + col;
            } else {
                // Linhas ímpares: direita para esquerda
                index = row * 5 + (4 - col);
            }
            led_buffer[index] = frames[number][row * 5 + col];
        }
    }
}

// Função para enviar um pixel para a matriz de LEDs
static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

// Função para converter valores de cor RGB em um formato compatível com WS2812
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(g) << 16) | ((uint32_t)(r) << 8) | (uint32_t)(b);
}

// Função para definir a cor dos LEDs na matriz
void set_one_led(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = urgb_u32(r, g, b);
    uint32_t black = urgb_u32(0, 0, 0);

    for (int i = 0; i < NUM_PIXELS; i++) {
        if (led_buffer[i]) {
            put_pixel(color); // Liga o LED
        } else {
            put_pixel(black); // Desliga o LED
        }
    }
}

// Funções de debounce usando alarmes (temporizadores)

// Callback para o botão de incremento
int64_t increment_alarm_callback(alarm_id_t id, void *user_data) {
    debounce_increment = false; // Permite nova leitura do botão
    return 0;
}

// Callback para o botão de decremento
int64_t decrement_alarm_callback(alarm_id_t id, void *user_data) {
    debounce_decrement = false; // Permite nova leitura do botão
    return 0;
}

// Callback das interrupções de GPIO
void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == BUTTON_INCREMENT_PIN && !debounce_increment) {
        debounce_increment = true; // Inicia o debounce
        // Inicia temporizador de debounce (200ms)
        add_alarm_in_ms(200, increment_alarm_callback, NULL, false);
        // Incrementa o número se ainda não chegou a 9
        if (current_number < 9) {
            current_number++;
            update_led_buffer(current_number);
        }
    } else if (gpio == BUTTON_DECREMENT_PIN && !debounce_decrement) {
        debounce_decrement = true; // Inicia o debounce
        // Inicia temporizador de debounce (200ms)
        add_alarm_in_ms(200, decrement_alarm_callback, NULL, false);
        // Decrementa o número se ainda não chegou a 0
        if (current_number > 0) {
            current_number--;
            update_led_buffer(current_number);
        }
    }
}

int main() {
    // Inicializa o PIO e o programa WS2812
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    // Inicializa o buffer de LEDs com o número 0
    update_led_buffer(current_number);

    // Configura os pinos dos botões como entrada
    gpio_init(BUTTON_INCREMENT_PIN);
    gpio_set_dir(BUTTON_INCREMENT_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_INCREMENT_PIN);

    gpio_init(BUTTON_DECREMENT_PIN);
    gpio_set_dir(BUTTON_DECREMENT_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_DECREMENT_PIN);

    // Configura interrupções nos pinos dos botões
    gpio_set_irq_enabled_with_callback(BUTTON_INCREMENT_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(BUTTON_DECREMENT_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Configura o pino do LED vermelho como saída
    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);

    while (1) {
        // Piscar o LED vermelho 5 vezes por segundo
        gpio_put(LED_RED_PIN, 1);
        sleep_ms(100); // 100ms ligado
        gpio_put(LED_RED_PIN, 0);
        sleep_ms(100); // 100ms desligado

        // Atualiza a matriz de LEDs com o número atual
        set_one_led(led_r, led_g, led_b);
    }

    return 0;
}
