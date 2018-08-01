#define BOARD_NAME "Boruit STM32F103"
#define BOARD_ID    0xc06a1f8e
/* echo -n "FST-01" | sha256sum | sed -e 's/^.*\(........\)  -$/\1/' */

#define STM32F10X_MD		/* Medium-density device */

#define STM32_PLLXTPRE                  STM32_PLLXTPRE_DIV1
#define STM32_PLLMUL_VALUE              9
#define STM32_HSECLK                    8000000

#define GPIO_LED_BASE   GPIOB_BASE
#define GPIO_LED_SET_TO_EMIT            0
#define GPIO_USB_BASE   GPIOA_BASE
#define GPIO_USB_SET_TO_ENABLE          10
#undef  GPIO_OTHER_BASE

/*
 * Port A setup.
 * PA0  - input with pull-up (TIM2_CH1): AN0 for NeuG
 * PA1  - input with pull-down (TIM2_CH2)
 * PA2  - input with pull-up (TIM2_CH3) connected to CIR module
 * PA3  - input with pull-up: external pin available to user
 * PA4  - Push pull output           (SPI1_NSS)
 * PA5  - Alternate Push pull output (SPI1_SCK)
 * PA6  - Alternate Push pull output (SPI1_MISO)
 * PA7  - Alternate Push pull output (SPI1_MOSI)
 * PA10 - Push pull output   (USB 1:ON 0:OFF)
 * PA11 - Push Pull output 10MHz 0 default (until USB enabled) (USBDM)
 * PA12 - Push Pull output 10MHz 0 default (until USB enabled) (USBDP)
 * ------------------------ Default
 * PA8  - input with pull-up.
 * PA9  - input with pull-up.
 * PA13 - input with pull-up.
 * PA14 - input with pull-up.
 * PA15 - input with pull-up.
 */
#define VAL_GPIO_USB_ODR            0xFFFFE7FD
#define VAL_GPIO_USB_CRL            0xBBB38888      /*  PA7...PA0 */
#define VAL_GPIO_USB_CRH            0x88811388      /* PA15...PA8 */

/*
 * Port B setup.
 * PB0  - Push pull output   (Green LED 1:ON 0:OFF)
 * PB1  - input with pull-up: AN9 for NeuG
 * PB2  - Push pull output   (Red LED 1:ON 0:OFF)
 * ------------------------ Default
 * PBx  - input with pull-up.
 */
#define VAL_GPIO_LED_ODR            0xFFFFFFFF
#define VAL_GPIO_LED_CRL            0x88888383      /*  PB7...PB0 */
#define VAL_GPIO_LED_CRH            0x88888888      /* PB15...PB8 */

#define RCC_ENR_IOP_EN      (RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN)
#define RCC_RSTR_IOP_RST    (RCC_APB2RSTR_IOPARST | RCC_APB2RSTR_IOPBRST)
