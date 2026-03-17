/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
        extern "C" {
        #endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (10)
#endif
/* ISR prototypes */
void r_icu_isr(void);
void sci_uart_rxi_isr(void);
void sci_uart_txi_isr(void);
void sci_uart_tei_isr(void);
void sci_uart_eri_isr(void);
void agt_int_isr(void);

/* Vector table allocations */
#define VECTOR_NUMBER_ICU_IRQ6 ((IRQn_Type) 0) /* ICU IRQ6 (External pin interrupt 6) */
#define ICU_IRQ6_IRQn          ((IRQn_Type) 0) /* ICU IRQ6 (External pin interrupt 6) */
#define VECTOR_NUMBER_SCI7_RXI ((IRQn_Type) 1) /* SCI7 RXI (Receive data full) */
#define SCI7_RXI_IRQn          ((IRQn_Type) 1) /* SCI7 RXI (Receive data full) */
#define VECTOR_NUMBER_SCI7_TXI ((IRQn_Type) 2) /* SCI7 TXI (Transmit data empty) */
#define SCI7_TXI_IRQn          ((IRQn_Type) 2) /* SCI7 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI7_TEI ((IRQn_Type) 3) /* SCI7 TEI (Transmit end) */
#define SCI7_TEI_IRQn          ((IRQn_Type) 3) /* SCI7 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI7_ERI ((IRQn_Type) 4) /* SCI7 ERI (Receive error) */
#define SCI7_ERI_IRQn          ((IRQn_Type) 4) /* SCI7 ERI (Receive error) */
#define VECTOR_NUMBER_SCI2_RXI ((IRQn_Type) 5) /* SCI2 RXI (Receive data full) */
#define SCI2_RXI_IRQn          ((IRQn_Type) 5) /* SCI2 RXI (Receive data full) */
#define VECTOR_NUMBER_SCI2_TXI ((IRQn_Type) 6) /* SCI2 TXI (Transmit data empty) */
#define SCI2_TXI_IRQn          ((IRQn_Type) 6) /* SCI2 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI2_TEI ((IRQn_Type) 7) /* SCI2 TEI (Transmit end) */
#define SCI2_TEI_IRQn          ((IRQn_Type) 7) /* SCI2 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI2_ERI ((IRQn_Type) 8) /* SCI2 ERI (Receive error) */
#define SCI2_ERI_IRQn          ((IRQn_Type) 8) /* SCI2 ERI (Receive error) */
#define VECTOR_NUMBER_AGT0_INT ((IRQn_Type) 9) /* AGT0 INT (AGT interrupt) */
#define AGT0_INT_IRQn          ((IRQn_Type) 9) /* AGT0 INT (AGT interrupt) */
#ifdef __cplusplus
        }
        #endif
#endif /* VECTOR_DATA_H */
