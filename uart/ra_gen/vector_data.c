/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = r_icu_isr, /* ICU IRQ6 (External pin interrupt 6) */
            [1] = sci_uart_rxi_isr, /* SCI7 RXI (Receive data full) */
            [2] = sci_uart_txi_isr, /* SCI7 TXI (Transmit data empty) */
            [3] = sci_uart_tei_isr, /* SCI7 TEI (Transmit end) */
            [4] = sci_uart_eri_isr, /* SCI7 ERI (Receive error) */
            [5] = sci_uart_rxi_isr, /* SCI2 RXI (Receive data full) */
            [6] = sci_uart_txi_isr, /* SCI2 TXI (Transmit data empty) */
            [7] = sci_uart_tei_isr, /* SCI2 TEI (Transmit end) */
            [8] = sci_uart_eri_isr, /* SCI2 ERI (Receive error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_ICU_IRQ6,GROUP0), /* ICU IRQ6 (External pin interrupt 6) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI7_RXI,GROUP1), /* SCI7 RXI (Receive data full) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI7_TXI,GROUP2), /* SCI7 TXI (Transmit data empty) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI7_TEI,GROUP3), /* SCI7 TEI (Transmit end) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_SCI7_ERI,GROUP4), /* SCI7 ERI (Receive error) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_SCI2_RXI,GROUP5), /* SCI2 RXI (Receive data full) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_SCI2_TXI,GROUP6), /* SCI2 TXI (Transmit data empty) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_SCI2_TEI,GROUP7), /* SCI2 TEI (Transmit end) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_SCI2_ERI,GROUP0), /* SCI2 ERI (Receive error) */
        };
        #endif
        #endif
