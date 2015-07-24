#define BOARD_NAME "ST Nucleo F103"
/* echo -n "ST Nucleo F103" | shasum -a 256 | sed -e 's/^.*\(........\)  -$/\1/' */
#define BOARD_ID    0x9b87c16d

// #define FLASH_PAGE_SIZE 1024
#define STM32F10X_MD		/* Medium-density device */

/* MB1136 C-01 does not use HSE */
#define STM32F103_HSE_NOT_USED          1

#if !defined(STM32F103_HSE_NOT_USED)
#define STM32_PLLXTPRE                  STM32_PLLXTPRE_DIV1
#define STM32_PLLMUL_VALUE              9
#define STM32_HSECLK                    8000000
/* external clock */
#define STM32F103_HSEBYP                1
#endif /* !defined(STM32F103_HSE_NOT_USED) */

#define GPIO_LED_BASE   GPIOA_BASE
#define GPIO_LED_SET_TO_EMIT            5
#define GPIO_USB_BASE   GPIOA_BASE
#define GPIO_USB_SET_TO_ENABLE          10
#undef  GPIO_OTHER_BASE

/* For pin-cir settings of Gnuk */
#define TIMx                  TIM2
#define INTR_REQ_TIM          TIM2_IRQ
#define AFIO_EXTICR_INDEX     0
#define AFIO_EXTICR1_EXTIx_Py AFIO_EXTICR1_EXTI2_PA
#define EXTI_PR               EXTI_PR_PR2
#define EXTI_IMR              EXTI_IMR_MR2
#define EXTI_FTSR_TR          EXTI_FTSR_TR2
#define INTR_REQ_EXTI         EXTI2_IRQ
#define ENABLE_RCC_APB1
#define RCC_APBnENR_TIMxEN    RCC_APB1ENR_TIM2EN
#define RCC_APBnRSTR_TIMxRST  RCC_APB1RSTR_TIM2RST

/*
 * Port A setup.
 * PA0  - input with pull-up.  AN0
 * PA1  - input with pull-up.  AN1
 * PA5  - Push pull output 50MHz (LED 1:ON 0:OFF)
 * PA10 - Push pull output 50MHz (USB 1:ON 0:OFF)
 * PA11 - Push Pull output 10MHz 0 default (until USB enabled) (USBDM) 
 * PA12 - Push Pull output 10MHz 0 default (until USB enabled) (USBDP)
 * ------------------------ Default
 * PAx  - input with pull-up
 */
#define VAL_GPIO_LED_ODR            0xFFFFE7FF
#define VAL_GPIO_LED_CRL            0x88388888      /*  PA7...PA0 */
#define VAL_GPIO_LED_CRH            0x88811388      /* PA15...PA8 */

#define RCC_ENR_IOP_EN      (RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN)
#define RCC_RSTR_IOP_RST    (RCC_APB2RSTR_IOPARST | RCC_APB2RSTR_IOPBRST)
