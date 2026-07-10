/**
 * @file storage_spi.h
 * @brief Conservative SPI transaction framing helpers for external flash.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_STORAGE_SPI_H
#define TAG_STORAGE_SPI_H

#include "core_types.h"
#include "spi_bus.h"

#include <stdint.h>

#ifndef TAG_STORAGE_SPI_POLL_LIMIT
#define TAG_STORAGE_SPI_POLL_LIMIT 100000U
#endif

#ifndef TAG_STORAGE_SPI_FAST
#define TAG_STORAGE_SPI_FAST 0
#endif

#ifndef TAG_STORAGE_SPI_FAST_BLOCK_READ
#define TAG_STORAGE_SPI_FAST_BLOCK_READ TAG_STORAGE_SPI_FAST
#endif

#ifndef TAG_STORAGE_SPI_FAST_BLOCK_WRITE
#define TAG_STORAGE_SPI_FAST_BLOCK_WRITE TAG_STORAGE_SPI_FAST
#endif

#ifndef TAG_STORAGE_SPI_DMA_BLOCK_READ
#define TAG_STORAGE_SPI_DMA_BLOCK_READ 0
#endif

#ifndef TAG_STORAGE_SPI_DMA_BLOCK_WRITE
#define TAG_STORAGE_SPI_DMA_BLOCK_WRITE 0
#endif

#ifndef TAG_STORAGE_SPI_DMA_SLEEP_WAIT
#define TAG_STORAGE_SPI_DMA_SLEEP_WAIT 1
#endif

#ifndef TAG_STORAGE_SPI_FAST_MAX_INFLIGHT
#define TAG_STORAGE_SPI_FAST_MAX_INFLIGHT 2U
#endif

#ifndef TAG_STORAGE_SPI_DMA_POLL_LIMIT
#define TAG_STORAGE_SPI_DMA_POLL_LIMIT 1000000U
#endif

#ifndef TAG_STORAGE_SPI_DMA_BYTE_POLL_LIMIT
#define TAG_STORAGE_SPI_DMA_BYTE_POLL_LIMIT 256U
#endif

#if defined(TAG_STORAGE_SPI_MEASURE_LINE)
static inline void tagStorageSpiMeasureInit(void)
{
  static bool initialized;

  if (!initialized) {
    palClearLine(TAG_STORAGE_SPI_MEASURE_LINE);
    palSetLineMode(TAG_STORAGE_SPI_MEASURE_LINE, PAL_MODE_OUTPUT_PUSHPULL);
    initialized = true;
  }
}

static inline void tagStorageSpiMeasureBegin(void)
{
  tagStorageSpiMeasureInit();
  palSetLine(TAG_STORAGE_SPI_MEASURE_LINE);
}

static inline void tagStorageSpiMeasureEnd(void)
{
  palClearLine(TAG_STORAGE_SPI_MEASURE_LINE);
}
#else
#define tagStorageSpiMeasureInit() do { } while (0)
#define tagStorageSpiMeasureBegin() do { } while (0)
#define tagStorageSpiMeasureEnd() do { } while (0)
#endif

static inline void tagStorageSpiRecover(SPI_TypeDef *spi)
{
  volatile uint32_t dummy;

  spi->CR1 &= ~SPI_CR1_SPE;

  while ((spi->SR & SPI_SR_RXNE) != 0U)
    dummy = spi->DR;

  dummy = spi->DR;
  dummy = spi->SR;
  (void)dummy;

  spi->CR1 |= SPI_CR1_SPE;
}

static inline bool tagStorageSpiWait(SPI_TypeDef *spi, uint32_t mask)
{
  uint32_t timeout = TAG_STORAGE_SPI_POLL_LIMIT;

  while ((spi->SR & mask) == 0U)
  {
    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
      tagStorageSpiRecover(spi);
      return false;
    }
    if (timeout-- == 0U) {
      tagStorageSpiRecover(spi);
      return false;
    }
  }
  return true;
}

static inline bool tagStorageSpiWaitIdle(SPI_TypeDef *spi)
{
  uint32_t timeout = TAG_STORAGE_SPI_POLL_LIMIT;
  uint32_t mask = SPI_SR_BSY;

#if defined(SPI_SR_FTLVL)
  mask |= SPI_SR_FTLVL;
#endif
  while ((spi->SR & mask) != 0U)
  {
    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
      tagStorageSpiRecover(spi);
      return false;
    }
    if (timeout-- == 0U) {
      tagStorageSpiRecover(spi);
      return false;
    }
  }
  return true;
}

static inline bool tagStorageSpiWriteFast(const TagSpiDevice *device,
                                          const uint8_t *buf, uint32_t n)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t sent = 0;
  uint32_t drained = 0;
  uint32_t timeout = TAG_STORAGE_SPI_POLL_LIMIT;

  while ((sent < n) || (drained < sent))
  {
    bool progress = false;

    while ((sent < n) &&
           ((sent - drained) < TAG_STORAGE_SPI_FAST_MAX_INFLIGHT) &&
           ((spi->SR & SPI_SR_TXE) != 0U))
    {
      *spidr = buf[sent++];
      progress = true;
    }

    while ((drained < sent) && ((spi->SR & SPI_SR_RXNE) != 0U))
    {
      (void)*spidr;
      drained++;
      progress = true;
    }

    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
      tagStorageSpiMeasureBegin();
      tagStorageSpiRecover(spi);
      return false;
    }

    if (progress) {
      timeout = TAG_STORAGE_SPI_POLL_LIMIT;
    } else if (timeout-- == 0U) {
      tagStorageSpiMeasureBegin();
      tagStorageSpiRecover(spi);
      return false;
    }
  }

  while ((spi->SR & SPI_SR_RXNE) != 0U)
    (void)*spidr;
  (void)spi->DR;
  (void)spi->SR;
  return true;
}

static inline bool tagStorageSpiReadFast(const TagSpiDevice *device,
                                         uint8_t *buf, uint32_t n)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t sent = 0;
  uint32_t received = 0;
  uint32_t timeout = TAG_STORAGE_SPI_POLL_LIMIT;

  while (received < n)
  {
    bool progress = false;

    if ((received < sent) && ((spi->SR & SPI_SR_RXNE) != 0U))
    {
      buf[received++] = *spidr;
      progress = true;
    }

    while ((sent < n) &&
           ((sent - received) < TAG_STORAGE_SPI_FAST_MAX_INFLIGHT) &&
           ((spi->SR & SPI_SR_TXE) != 0U))
    {
      *spidr = device->dummy;
      sent++;
      progress = true;
    }

    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
      tagStorageSpiMeasureBegin();
      tagStorageSpiRecover(spi);
      return false;
    }

    if (progress) {
      timeout = TAG_STORAGE_SPI_POLL_LIMIT;
    } else if (timeout-- == 0U) {
      tagStorageSpiMeasureBegin();
      tagStorageSpiRecover(spi);
      return false;
    }
  }
  return true;
}

static inline bool tagStorageSpiRead(const TagSpiDevice *device, uint8_t *buf,
                                     uint32_t n);
static inline bool tagStorageSpiWrite(const TagSpiDevice *device,
                                      const uint8_t *buf, uint32_t n);

#if (TAG_STORAGE_SPI_DMA_BLOCK_READ || TAG_STORAGE_SPI_DMA_BLOCK_WRITE) && \
    STM32_DMA_SUPPORTS_CSELR
typedef struct {
  volatile uint32_t flags;
  volatile bool complete;
  volatile bool error;
} TagStorageSpiDmaIrqState;

static const stm32_dma_stream_t *tag_storage_spi_rx_dma;
static const stm32_dma_stream_t *tag_storage_spi_tx_dma;
static TagStorageSpiDmaIrqState tag_storage_spi_rx_dma_irq;
static TagStorageSpiDmaIrqState tag_storage_spi_tx_dma_irq;

static void tagStorageSpiDmaInterrupt(void *param, uint32_t flags)
{
  TagStorageSpiDmaIrqState *state = (TagStorageSpiDmaIrqState *)param;

  state->flags |= flags;
  if ((flags & STM32_DMA_ISR_TCIF) != 0U)
    state->complete = true;
  if ((flags & STM32_DMA_ISR_TEIF) != 0U)
    state->error = true;
  __DMB();
}

static inline bool tagStorageSpiDmaConfig(const TagSpiDevice *device,
                                          uint32_t *rx_stream,
                                          uint32_t *tx_stream,
                                          uint32_t *rx_channel,
                                          uint32_t *tx_channel,
                                          uint32_t *priority,
                                          uint32_t *irq_priority)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);

  if (spi == SPI1) {
    *rx_stream = STM32_SPI_SPI1_RX_DMA_STREAM;
    *tx_stream = STM32_SPI_SPI1_TX_DMA_STREAM;
    *rx_channel = STM32_DMA_GETCHANNEL(*rx_stream, STM32_SPI1_RX_DMA_CHN);
    *tx_channel = STM32_DMA_GETCHANNEL(*tx_stream, STM32_SPI1_TX_DMA_CHN);
    *priority = STM32_SPI_SPI1_DMA_PRIORITY;
#if defined(STM32_SPI_SPI1_IRQ_PRIORITY)
    *irq_priority = STM32_SPI_SPI1_IRQ_PRIORITY;
#else
    *irq_priority = 0U;
#endif
    return true;
  }

#if defined(SPI3) && STM32_SPI_USE_SPI3 && \
    defined(STM32_SPI3_RX_DMA_CHN) && defined(STM32_SPI3_TX_DMA_CHN)
  if (spi == SPI3) {
    *rx_stream = STM32_SPI_SPI3_RX_DMA_STREAM;
    *tx_stream = STM32_SPI_SPI3_TX_DMA_STREAM;
    *rx_channel = STM32_DMA_GETCHANNEL(*rx_stream, STM32_SPI3_RX_DMA_CHN);
    *tx_channel = STM32_DMA_GETCHANNEL(*tx_stream, STM32_SPI3_TX_DMA_CHN);
    *priority = STM32_SPI_SPI3_DMA_PRIORITY;
#if defined(STM32_SPI_SPI3_IRQ_PRIORITY)
    *irq_priority = STM32_SPI_SPI3_IRQ_PRIORITY;
#else
    *irq_priority = 0U;
#endif
    return true;
  }
#endif

  return false;
}

static inline bool tagStorageSpiDmaEnsureStreams(uint32_t rx_stream,
                                                 uint32_t tx_stream,
                                                 uint32_t irq_priority)
{
  bool allocated_rx = false;

  if (tag_storage_spi_rx_dma == NULL) {
    tag_storage_spi_rx_dma = dmaStreamAlloc(rx_stream, irq_priority,
                                            tagStorageSpiDmaInterrupt,
                                            &tag_storage_spi_rx_dma_irq);
    if (tag_storage_spi_rx_dma == NULL)
      return false;
    allocated_rx = true;
  }

  if (tag_storage_spi_tx_dma == NULL) {
    tag_storage_spi_tx_dma = dmaStreamAlloc(tx_stream, irq_priority,
                                            tagStorageSpiDmaInterrupt,
                                            &tag_storage_spi_tx_dma_irq);
    if (tag_storage_spi_tx_dma == NULL) {
      if (allocated_rx) {
        dmaStreamFree(tag_storage_spi_rx_dma);
        tag_storage_spi_rx_dma = NULL;
      }
      return false;
    }
  }

  return true;
}

static inline uint32_t tagStorageSpiDmaTimeout(uint32_t units)
{
  return TAG_STORAGE_SPI_DMA_POLL_LIMIT +
         (units * TAG_STORAGE_SPI_DMA_BYTE_POLL_LIMIT);
}

static inline uint32_t tagStorageSpiDmaFlags(const stm32_dma_stream_t *dmastp)
{
  return (dmastp->dma->ISR >> dmastp->shift) & STM32_DMA_ISR_MASK;
}

static inline bool tagStorageSpiDmaPollComplete(const stm32_dma_stream_t *dmastp,
                                                uint32_t units)
{
  uint32_t timeout = tagStorageSpiDmaTimeout(units);

  while (dmaStreamGetTransactionSize(dmastp) != 0U)
  {
    uint32_t flags = tagStorageSpiDmaFlags(dmastp);

    if ((flags & STM32_DMA_ISR_TEIF) != 0U)
      return false;
    if (timeout-- == 0U)
      return false;
  }
  return true;
}

#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
typedef struct {
  uint32_t ahb1_mask;
  uint32_t ahb1_bits;
  uint32_t apb1_mask;
  uint32_t apb1_bits;
  uint32_t apb2_mask;
  uint32_t apb2_bits;
} TagStorageSpiDmaSleepClockState;

static inline TagStorageSpiDmaSleepClockState tagStorageSpiDmaEnableSleepClocks(
    SPI_TypeDef *spi)
{
  TagStorageSpiDmaSleepClockState state = {0};

#if defined(DMA1) && defined(RCC_AHB1SMENR_DMA1SMEN)
  if ((tag_storage_spi_rx_dma->dma == DMA1) ||
      (tag_storage_spi_tx_dma->dma == DMA1)) {
    state.ahb1_mask |= RCC_AHB1SMENR_DMA1SMEN;
    state.ahb1_bits |= RCC->AHB1SMENR & RCC_AHB1SMENR_DMA1SMEN;
    RCC->AHB1SMENR |= RCC_AHB1SMENR_DMA1SMEN;
  }
#endif
#if defined(DMA2) && defined(RCC_AHB1SMENR_DMA2SMEN)
  if ((tag_storage_spi_rx_dma->dma == DMA2) ||
      (tag_storage_spi_tx_dma->dma == DMA2)) {
    state.ahb1_mask |= RCC_AHB1SMENR_DMA2SMEN;
    state.ahb1_bits |= RCC->AHB1SMENR & RCC_AHB1SMENR_DMA2SMEN;
    RCC->AHB1SMENR |= RCC_AHB1SMENR_DMA2SMEN;
  }
#endif
#if defined(SPI1) && defined(RCC_APB2SMENR_SPI1SMEN)
  if (spi == SPI1) {
    state.apb2_mask |= RCC_APB2SMENR_SPI1SMEN;
    state.apb2_bits |= RCC->APB2SMENR & RCC_APB2SMENR_SPI1SMEN;
    RCC->APB2SMENR |= RCC_APB2SMENR_SPI1SMEN;
  }
#endif
#if defined(SPI3) && defined(RCC_APB1SMENR1_SPI3SMEN)
  if (spi == SPI3) {
    state.apb1_mask |= RCC_APB1SMENR1_SPI3SMEN;
    state.apb1_bits |= RCC->APB1SMENR1 & RCC_APB1SMENR1_SPI3SMEN;
    RCC->APB1SMENR1 |= RCC_APB1SMENR1_SPI3SMEN;
  }
#endif
  __DSB();
  return state;
}

static inline void tagStorageSpiDmaRestoreSleepClocks(
    TagStorageSpiDmaSleepClockState state)
{
  RCC->AHB1SMENR = (RCC->AHB1SMENR & ~state.ahb1_mask) | state.ahb1_bits;
  RCC->APB1SMENR1 = (RCC->APB1SMENR1 & ~state.apb1_mask) | state.apb1_bits;
  RCC->APB2SMENR = (RCC->APB2SMENR & ~state.apb2_mask) | state.apb2_bits;
  __DSB();
}

static inline void tagStorageSpiDmaRestoreSleepDeep(uint32_t sleepdeep)
{
  if (sleepdeep != 0U)
    SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
}

static inline bool tagStorageSpiDmaSleepComplete(const stm32_dma_stream_t *dmastp,
                                                 uint32_t units)
{
  uint32_t timeout = tagStorageSpiDmaTimeout(units);
  uint32_t sleepdeep = SCB->SCR & SCB_SCR_SLEEPDEEP_Msk;

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  while (!tag_storage_spi_rx_dma_irq.complete)
  {
    uint32_t rx_flags = tag_storage_spi_rx_dma_irq.flags |
                        tagStorageSpiDmaFlags(dmastp);
    uint32_t tx_flags = tag_storage_spi_tx_dma_irq.flags |
                        tagStorageSpiDmaFlags(tag_storage_spi_tx_dma);
    uint32_t flags = rx_flags | tx_flags;

    if (((flags & STM32_DMA_ISR_TEIF) != 0U) ||
        tag_storage_spi_rx_dma_irq.error ||
        tag_storage_spi_tx_dma_irq.error) {
      tagStorageSpiDmaRestoreSleepDeep(sleepdeep);
      return false;
    }
    if (timeout-- == 0U) {
      tagStorageSpiDmaRestoreSleepDeep(sleepdeep);
      return false;
    }

    __DSB();
    __WFI();
    __DMB();
  }

  if (((tag_storage_spi_rx_dma_irq.flags | tag_storage_spi_tx_dma_irq.flags |
        tagStorageSpiDmaFlags(dmastp) |
        tagStorageSpiDmaFlags(tag_storage_spi_tx_dma)) &
       STM32_DMA_ISR_TEIF) != 0U) {
    tagStorageSpiDmaRestoreSleepDeep(sleepdeep);
    return false;
  }

  tagStorageSpiDmaRestoreSleepDeep(sleepdeep);
  return true;
}

static inline bool tagStorageSpiDmaWaitComplete(const stm32_dma_stream_t *dmastp,
                                                uint32_t units)
{
  if (MONCONNECTED)
    return tagStorageSpiDmaPollComplete(dmastp, units);
  return tagStorageSpiDmaSleepComplete(dmastp, units);
}
#else
static inline bool tagStorageSpiDmaWaitComplete(const stm32_dma_stream_t *dmastp,
                                                uint32_t units)
{
  return tagStorageSpiDmaPollComplete(dmastp, units);
}
#endif

static inline void tagStorageSpiDmaStop(SPI_TypeDef *spi)
{
  spi->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
  dmaStreamDisable(tag_storage_spi_rx_dma);
  dmaStreamDisable(tag_storage_spi_tx_dma);
}
#endif

#if TAG_STORAGE_SPI_DMA_BLOCK_READ && STM32_DMA_SUPPORTS_CSELR
static inline bool tagStorageSpiBlockReadDma(const TagSpiDevice *device,
                                             uint8_t *buf, uint32_t n)
{
  static uint8_t dummy;

  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  uint32_t rx_stream;
  uint32_t tx_stream;
  uint32_t rx_channel;
  uint32_t tx_channel;
  uint32_t priority;
  uint32_t irq_priority;
  uint32_t rx_mode;
  uint32_t tx_mode;
  bool ok;

  if (n == 0U)
    return true;

  if (n > STM32_DMA_MAX_TRANSFER)
    return false;

  if (!tagStorageSpiDmaConfig(device, &rx_stream, &tx_stream, &rx_channel,
                              &tx_channel, &priority, &irq_priority))
    return tagStorageSpiRead(device, buf, n);

  if (!tagStorageSpiDmaEnsureStreams(rx_stream, tx_stream, irq_priority))
    return tagStorageSpiRead(device, buf, n);

  tagStorageSpiRecover(spi);
  dummy = device->dummy;
  tag_storage_spi_rx_dma_irq.flags = 0U;
  tag_storage_spi_rx_dma_irq.complete = false;
  tag_storage_spi_rx_dma_irq.error = false;
  tag_storage_spi_tx_dma_irq.flags = 0U;
  tag_storage_spi_tx_dma_irq.complete = false;
  tag_storage_spi_tx_dma_irq.error = false;

  dmaStreamClearInterrupt(tag_storage_spi_rx_dma);
  dmaStreamClearInterrupt(tag_storage_spi_tx_dma);

  rx_mode = STM32_DMA_CR_CHSEL(rx_channel) |
            STM32_DMA_CR_PL(priority) |
            STM32_DMA_CR_MINC |
            STM32_DMA_CR_PSIZE_BYTE |
            STM32_DMA_CR_MSIZE_BYTE;
  tx_mode = STM32_DMA_CR_CHSEL(tx_channel) |
            STM32_DMA_CR_PL(priority) |
            STM32_DMA_CR_DIR_M2P |
            STM32_DMA_CR_PSIZE_BYTE |
            STM32_DMA_CR_MSIZE_BYTE;
#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
  if (!MONCONNECTED) {
    rx_mode |= STM32_DMA_CR_TCIE | STM32_DMA_CR_TEIE;
    tx_mode |= STM32_DMA_CR_TEIE;
  }
#endif

  dmaStreamSetPeripheral(tag_storage_spi_rx_dma, &spi->DR);
  dmaStreamSetMemory0(tag_storage_spi_rx_dma, buf);
  dmaStreamSetTransactionSize(tag_storage_spi_rx_dma, n);
  dmaStreamSetMode(tag_storage_spi_rx_dma, rx_mode);

  dmaStreamSetPeripheral(tag_storage_spi_tx_dma, &spi->DR);
  dmaStreamSetMemory0(tag_storage_spi_tx_dma, &dummy);
  dmaStreamSetTransactionSize(tag_storage_spi_tx_dma, n);
  dmaStreamSetMode(tag_storage_spi_tx_dma, tx_mode);

  tagStorageSpiMeasureBegin();
  spi->CR2 |= SPI_CR2_RXDMAEN;
  dmaStreamEnable(tag_storage_spi_rx_dma);
  dmaStreamEnable(tag_storage_spi_tx_dma);
  spi->CR2 |= SPI_CR2_TXDMAEN;

#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
  TagStorageSpiDmaSleepClockState sleep_clocks =
      tagStorageSpiDmaEnableSleepClocks(spi);
#endif
  ok = tagStorageSpiDmaWaitComplete(tag_storage_spi_rx_dma, n);
#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
  tagStorageSpiDmaRestoreSleepClocks(sleep_clocks);
#endif
  tagStorageSpiDmaStop(spi);
  tagStorageSpiMeasureEnd();

  if (!ok) {
    tagStorageSpiRecover(spi);
    return false;
  }

  return true;
}
#endif

#if TAG_STORAGE_SPI_DMA_BLOCK_WRITE && STM32_DMA_SUPPORTS_CSELR
static inline bool tagStorageSpiBlockWriteDma(const TagSpiDevice *device,
                                              const uint8_t *buf, uint32_t n)
{
  static uint8_t sink;

  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  uint32_t rx_stream;
  uint32_t tx_stream;
  uint32_t rx_channel;
  uint32_t tx_channel;
  uint32_t priority;
  uint32_t irq_priority;
  uint32_t rx_mode;
  uint32_t tx_mode;
  bool ok;

  if (n == 0U)
    return true;

  if (n > STM32_DMA_MAX_TRANSFER)
    return false;

  if (!tagStorageSpiDmaConfig(device, &rx_stream, &tx_stream, &rx_channel,
                              &tx_channel, &priority, &irq_priority))
    return tagStorageSpiWrite(device, buf, n);

  if (!tagStorageSpiDmaEnsureStreams(rx_stream, tx_stream, irq_priority))
    return tagStorageSpiWrite(device, buf, n);

  tagStorageSpiRecover(spi);
  sink = 0U;
  tag_storage_spi_rx_dma_irq.flags = 0U;
  tag_storage_spi_rx_dma_irq.complete = false;
  tag_storage_spi_rx_dma_irq.error = false;
  tag_storage_spi_tx_dma_irq.flags = 0U;
  tag_storage_spi_tx_dma_irq.complete = false;
  tag_storage_spi_tx_dma_irq.error = false;

  dmaStreamClearInterrupt(tag_storage_spi_rx_dma);
  dmaStreamClearInterrupt(tag_storage_spi_tx_dma);

  rx_mode = STM32_DMA_CR_CHSEL(rx_channel) |
            STM32_DMA_CR_PL(priority) |
            STM32_DMA_CR_PSIZE_BYTE |
            STM32_DMA_CR_MSIZE_BYTE;
  tx_mode = STM32_DMA_CR_CHSEL(tx_channel) |
            STM32_DMA_CR_PL(priority) |
            STM32_DMA_CR_DIR_M2P |
            STM32_DMA_CR_MINC |
            STM32_DMA_CR_PSIZE_BYTE |
            STM32_DMA_CR_MSIZE_BYTE;
#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
  if (!MONCONNECTED) {
    rx_mode |= STM32_DMA_CR_TCIE | STM32_DMA_CR_TEIE;
    tx_mode |= STM32_DMA_CR_TEIE;
  }
#endif

  dmaStreamSetPeripheral(tag_storage_spi_rx_dma, &spi->DR);
  dmaStreamSetMemory0(tag_storage_spi_rx_dma, &sink);
  dmaStreamSetTransactionSize(tag_storage_spi_rx_dma, n);
  dmaStreamSetMode(tag_storage_spi_rx_dma, rx_mode);

  dmaStreamSetPeripheral(tag_storage_spi_tx_dma, &spi->DR);
  dmaStreamSetMemory0(tag_storage_spi_tx_dma, buf);
  dmaStreamSetTransactionSize(tag_storage_spi_tx_dma, n);
  dmaStreamSetMode(tag_storage_spi_tx_dma, tx_mode);

  tagStorageSpiMeasureBegin();
  spi->CR2 |= SPI_CR2_RXDMAEN;
  dmaStreamEnable(tag_storage_spi_rx_dma);
  dmaStreamEnable(tag_storage_spi_tx_dma);
  spi->CR2 |= SPI_CR2_TXDMAEN;

#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
  TagStorageSpiDmaSleepClockState sleep_clocks =
      tagStorageSpiDmaEnableSleepClocks(spi);
#endif
  ok = tagStorageSpiDmaWaitComplete(tag_storage_spi_rx_dma, n);
#if TAG_STORAGE_SPI_DMA_SLEEP_WAIT
  tagStorageSpiDmaRestoreSleepClocks(sleep_clocks);
#endif
  tagStorageSpiDmaStop(spi);
  tagStorageSpiMeasureEnd();

  if (!ok) {
    tagStorageSpiRecover(spi);
    return false;
  }

  return true;
}
#endif

/** @name Storage SPI transaction helpers
 * SPI transaction helpers for external flash drivers.
 *
 * Flash chips keep chip select asserted across command, optional address, and
 * data phases. These helpers centralize that framing while leaving chip
 * commands and polling rules in the individual storage drivers.
 *
 * These helpers deliberately use conservative byte-at-a-time transfers rather
 * than the pipelined stream helpers in spi_bus.c. The flash erase/write command
 * path has historically depended on reading each full-duplex byte before
 * sending the next byte; keeping that pacing here preserves the known-good
 * storage behavior while the generic SPI layer remains available for devices
 * that tolerate streaming transactions.
 * @{
 */

/**
 * @brief Write bytes to flash using byte-at-a-time full-duplex pacing.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] buf Bytes to transmit.
 * @param[in] n Number of bytes to transmit.
 */
static inline bool tagStorageSpiWrite(const TagSpiDevice *device,
                                      const uint8_t *buf, uint32_t n)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (n--)
  {
    *spidr = *buf++;
    if (!tagStorageSpiWait(spi, SPI_SR_RXNE))
      return false;
    (void)*spidr;
  }
  return true;
}

/**
 * @brief Read bytes from flash while keeping the SPI clock continuously fed.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[out] buf Destination buffer.
 * @param[in] n Number of bytes to read.
 */
static inline bool tagStorageSpiRead(const TagSpiDevice *device, uint8_t *buf,
                                     uint32_t n)
{
    SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
    volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

    if (n == 0) {
        return true;
    }

    /* --- Prime the pipeline: send first dummy byte before entering loop --- */
    *spidr = device->dummy;

    while (n-- > 1)
    {
        /*
         * Send the NEXT dummy byte as soon as TXE is set — this keeps the
         * SPI bus continuously clocked while we wait for the current RX byte.
         * TXE rises the moment the shift register has accepted the byte from
         * DR, long before RXNE rises for that same byte, so there is no gap.
        */
        if (!tagStorageSpiWait(spi, SPI_SR_TXE)) {
            return false;
        }
        *spidr = device->dummy;

        /* Now collect the byte that was clocked in for the previous write. */
        if (!tagStorageSpiWait(spi, SPI_SR_RXNE)) {
            return false;
        }
        *buf++ = *spidr;
    }

    /* --- Drain the last byte that is still in flight --- */
    if (!tagStorageSpiWait(spi, SPI_SR_RXNE)) {
        return false;
    }
    *buf = *spidr;
    return true;
}

/**
 * @brief Bulk write bytes to flash after command/address framing.
 *
 * The default implementation preserves the conservative byte-paced write.
 * Targets can opt in to a different implementation for long data phases
 * without changing command and address timing.
 */
static inline bool tagStorageSpiBlockWrite(const TagSpiDevice *device,
                                           const uint8_t *buf, uint32_t n)
{
#if TAG_STORAGE_SPI_DMA_BLOCK_WRITE && STM32_DMA_SUPPORTS_CSELR
  return tagStorageSpiBlockWriteDma(device, buf, n);
#elif TAG_STORAGE_SPI_FAST_BLOCK_WRITE
  return tagStorageSpiWriteFast(device, buf, n);
#else
  return tagStorageSpiWrite(device, buf, n);
#endif
}

/**
 * @brief Bulk read bytes from flash after command/address framing.
 *
 * The default implementation preserves the conservative one-byte-ahead read.
 * This is the future hook for DMA or a proven-safe FIFO implementation.
 */
static inline bool tagStorageSpiBlockRead(const TagSpiDevice *device,
                                          uint8_t *buf, uint32_t n)
{
#if TAG_STORAGE_SPI_DMA_BLOCK_READ && STM32_DMA_SUPPORTS_CSELR
  return tagStorageSpiBlockReadDma(device, buf, n);
#elif TAG_STORAGE_SPI_FAST_BLOCK_READ
  return tagStorageSpiReadFast(device, buf, n);
#else
  return tagStorageSpiRead(device, buf, n);
#endif
}

/**
 * @brief Send a 24-bit flash address in command byte order.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] address 24-bit flash address.
 */
static inline bool tagStorageSpiAddress(const TagSpiDevice *device,
                                        uint32_t address)
{
  uint8_t buf[3];

  buf[0] = address >> 16;
  buf[1] = address >> 8;
  buf[2] = address & 0xff;
  return tagStorageSpiWrite(device, buf, sizeof(buf));
}

/**
 * @brief Send one standalone flash command under chip select.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 */
static inline bool tagStorageSpiCommand(const TagSpiDevice *device, uint8_t cmd)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a flash command and 24-bit address under chip select.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 */
static inline bool tagStorageSpiCommandAddress(const TagSpiDevice *device,
                                               uint8_t cmd,
                                               uint32_t address)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiAddress(device, address);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a flash command and receive response bytes under chip select.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of response bytes to read.
 */
static inline bool tagStorageSpiCommandReceive(const TagSpiDevice *device,
                                               uint8_t cmd, uint8_t *buf,
                                               uint32_t num)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiBlockRead(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a command/address pair and receive response bytes.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of response bytes to read.
 */
static inline bool tagStorageSpiCommandAddressReceive(
    const TagSpiDevice *device,
                                                      uint8_t cmd,
                                                      uint32_t address,
                                                      uint8_t *buf,
                                                      uint32_t num)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiAddress(device, address) &&
       tagStorageSpiBlockRead(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a command/address pair followed by payload bytes.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 * @param[in] buf Payload bytes to send.
 * @param[in] num Number of payload bytes to send.
 */
static inline bool tagStorageSpiCommandAddressSend(const TagSpiDevice *device,
                                                   uint8_t cmd,
                                                   uint32_t address,
                                                   const uint8_t *buf,
                                                   uint32_t num)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiAddress(device, address) &&
       tagStorageSpiBlockWrite(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}
/** @} */

#endif
