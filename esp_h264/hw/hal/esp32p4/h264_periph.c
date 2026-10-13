/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include <stddef.h>
#include <stdint.h>
#include "h264_ll.h"
#include "h264_dma_ll.h"
#include "h264_periph.h"

#if CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP

/*
 * REGDMA link order: **H264 DMA first, then H264 encoder core** (save/restore sequence).
 *
 * H.264 core (h264_dev_t): retain non-debug MMIO, excluding slice_header_remain,
 *   slice_header_byte_length, slice_header[2], frame_code_length and date.
 *   - Head A1: through rc_status2 (stops before slice_header_remain)
 *   - Head A2: bs_threshold only
 *   - Head B1a: int_raw and int_st; B1b: int_ena and int_clr
 *   - Head B2: conf .. mv_merge_config (before debug_dma_sel; debug_dma_sel is
 *     the TOP-PD canary — do not add it to REGDMA)
 *   - Mid: sys_status only
 *   - Tail (rev≥300): ori_conf[2] only
 */
#define H264_CORE_BASE          ((uint32_t)(uintptr_t)H264_LL_GET_HW())
#define H264_CORE_HEAD_A1_WORDS ((int)(offsetof(h264_dev_t, slice_header_remain) / sizeof(uint32_t)))
#define H264_CORE_HEAD_A2_BASE  (H264_CORE_BASE + (uint32_t)offsetof(h264_dev_t, bs_threshold))
#define H264_CORE_HEAD_A2_WORDS ((int)((offsetof(h264_dev_t, slice_header) - offsetof(h264_dev_t, bs_threshold)) / sizeof(uint32_t)))
#define H264_CORE_HEAD_B1A_BASE  (H264_CORE_BASE + (uint32_t)offsetof(h264_dev_t, int_raw))
#define H264_CORE_HEAD_B1A_WORDS ((int)((offsetof(h264_dev_t, int_ena) - offsetof(h264_dev_t, int_raw)) / sizeof(uint32_t)))
#define H264_CORE_HEAD_B1B_BASE  (H264_CORE_BASE + (uint32_t)offsetof(h264_dev_t, int_ena))
#define H264_CORE_HEAD_B1B_WORDS ((int)((offsetof(h264_dev_t, conf) - offsetof(h264_dev_t, int_ena)) / sizeof(uint32_t)))
#define H264_CORE_HEAD_B2_BASE  (H264_CORE_BASE + (uint32_t)offsetof(h264_dev_t, conf))
#define H264_CORE_HEAD_B2_WORDS ((int)((offsetof(h264_dev_t, debug_dma_sel) - offsetof(h264_dev_t, conf)) / sizeof(uint32_t)))
#define H264_CORE_MID_BASE     (H264_CORE_BASE + (uint32_t)offsetof(h264_dev_t, sys_status))
#define H264_CORE_MID_WORDS    ((int)((offsetof(h264_dev_t, frame_code_length) - offsetof(h264_dev_t, sys_status)) / sizeof(uint32_t)))
#if HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300
#define H264_CORE_TAIL_BASE  (H264_CORE_BASE + (uint32_t)offsetof(h264_dev_t, ori_conf))
#define H264_CORE_TAIL_WORDS ((int)((offsetof(h264_dev_t, ori_debug_conf) - offsetof(h264_dev_t, ori_conf)) / sizeof(uint32_t)))
#endif /* HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300 */

_Static_assert(offsetof(h264_dev_t, slice_header_remain) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, bs_threshold) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, slice_header) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, int_raw) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, int_st) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, int_ena) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, int_clr) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, conf) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, mv_merge_config) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, debug_dma_sel) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, sys_status) % sizeof(uint32_t) == 0, "h264_dev_t layout");
_Static_assert(offsetof(h264_dev_t, bs_threshold) - offsetof(h264_dev_t, slice_header_remain) ==
               sizeof(((h264_dev_t *)0)->slice_header_remain) +
               sizeof(((h264_dev_t *)0)->slice_header_byte_length),
               "slice_header_remain/byte_length before bs_threshold");
_Static_assert(offsetof(h264_dev_t, slice_header) - offsetof(h264_dev_t, bs_threshold) ==
               sizeof(((h264_dev_t *)0)->bs_threshold), "bs_threshold before slice_header");
_Static_assert(offsetof(h264_dev_t, int_raw) - offsetof(h264_dev_t, slice_header) ==
               sizeof(((h264_dev_t *)0)->slice_header), "slice_header before int_raw");
_Static_assert(offsetof(h264_dev_t, int_st) - offsetof(h264_dev_t, int_raw) ==
               sizeof(((h264_dev_t *)0)->int_raw), "int_raw before int_st");
_Static_assert(offsetof(h264_dev_t, int_ena) - offsetof(h264_dev_t, int_st) ==
               sizeof(((h264_dev_t *)0)->int_st), "int_st before int_ena");
_Static_assert(offsetof(h264_dev_t, int_clr) - offsetof(h264_dev_t, int_ena) ==
               sizeof(((h264_dev_t *)0)->int_ena), "int_ena before int_clr");
_Static_assert(offsetof(h264_dev_t, conf) - offsetof(h264_dev_t, int_clr) ==
               sizeof(((h264_dev_t *)0)->int_clr), "int_clr before conf");
_Static_assert(offsetof(h264_dev_t, mv_merge_config) - offsetof(h264_dev_t, conf) ==
               sizeof(((h264_dev_t *)0)->conf), "conf before mv_merge_config");
_Static_assert(offsetof(h264_dev_t, debug_dma_sel) - offsetof(h264_dev_t, mv_merge_config) ==
               sizeof(((h264_dev_t *)0)->mv_merge_config), "mv_merge_config before debug_dma_sel");
_Static_assert((size_t)H264_CORE_HEAD_A2_WORDS *sizeof(uint32_t) ==
               sizeof(((h264_dev_t *)0)->bs_threshold), "Head A2 is bs_threshold only");
_Static_assert((size_t)H264_CORE_HEAD_B1A_WORDS *sizeof(uint32_t) ==
               sizeof(((h264_dev_t *)0)->int_raw) + sizeof(((h264_dev_t *)0)->int_st),
               "B1a is int_raw and int_st");
_Static_assert((size_t)H264_CORE_HEAD_B1B_WORDS *sizeof(uint32_t) ==
               sizeof(((h264_dev_t *)0)->int_ena) + sizeof(((h264_dev_t *)0)->int_clr),
               "B1b is int_ena and int_clr");
_Static_assert((size_t)H264_CORE_HEAD_B2_WORDS *sizeof(uint32_t) ==
               sizeof(((h264_dev_t *)0)->conf) + sizeof(((h264_dev_t *)0)->mv_merge_config),
               "Head B2 is conf through mv_merge_config");
_Static_assert(H264_CORE_MID_WORDS == 1, "sys_status must be single word before frame_code_length");

/*
 * --- H.264 dedicated DMA (unchanged addr-map links) ---
 */
#define H264_DMA_RETENTION_BASE      ((uint32_t)(uintptr_t)H264_DMA_LL_GET_HW())
#define H264_DMA_OUT_CH_BASE(n)      (H264_DMA_RETENTION_BASE + (uint32_t)offsetof(h264_dma_dev_t, dma_out_ch[(n)]))
#define H264_DMA_IN_CH_BASE(n)       (H264_DMA_RETENTION_BASE + (uint32_t)offsetof(h264_dma_dev_t, dma_in_ch[(n)]))
#define H264_DMA_IN_CH5_BASE         (H264_DMA_RETENTION_BASE + (uint32_t)offsetof(h264_dma_dev_t, dma_in_ch5))
#define H264_DMA_GLOBAL_BASE         (H264_DMA_RETENTION_BASE + (uint32_t)offsetof(h264_dma_dev_t, rst_conf))
#define H264_DMA_OUT_CH_REGS_CNT     8U
#define H264_DMA_IN_CH_REGS_CNT      7U
#define H264_DMA_IN_CH5_REGS_CNT     7U
#define H264_DMA_GLOBAL_REGS_CNT     12U

static const uint32_t h264_dma_out_ch_map[4] = {0x90008185U, 0x1U, 0U, 0U};
static const uint32_t h264_dma_in_ch_map[4]  = {0x50185U, 0x4U, 0U, 0U};
static const uint32_t h264_dma_in_ch5_map[4] = {0x1002fU, 0x4U, 0U, 0U};
static const uint32_t h264_dma_global_map[4] = {0x800fdfU, 0U, 0U, 0U};

/* DMA links before core: OUT [0..4], IN [5..9], in5 [10], global(rst_conf..) [11], pbyte [12] (rev≥300 only). */
#if HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300
#define H264_DMA_PBYTE_BASE    (H264_DMA_RETENTION_BASE + (uint32_t)offsetof(h264_dma_dev_t, pbyte))
#define H264_DMA_LINK_PBYTE \
    [12] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x16), \
                                              (void *)(uintptr_t)H264_DMA_PBYTE_BASE, \
                                              (void *)(uintptr_t)H264_DMA_PBYTE_BASE, \
                                              1, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    },
#define H264_CORE_IDX0 13
_Static_assert(offsetof(h264_dma_dev_t, pbyte) % sizeof(uint32_t) == 0, "h264_dma_dev_t pbyte align");
#else
#define H264_DMA_LINK_PBYTE
#define H264_CORE_IDX0 12
#endif /* HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300 */
#define H264_DMA_FIRST_OUT(i, pri) \
    [(i)] = { \
        .config = REGDMA_LINK_ADDR_MAP_INIT(REGDMA_H264_LINK(pri), \
                                            H264_DMA_OUT_CH_BASE(i), H264_DMA_OUT_CH_BASE(i), \
                                            H264_DMA_OUT_CH_REGS_CNT, 0, 0, \
                                            h264_dma_out_ch_map[0], h264_dma_out_ch_map[1], \
                                            h264_dma_out_ch_map[2], h264_dma_out_ch_map[3]), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_DMA_FIRST_IN(i, pri) \
    [(i) + 5] = { \
        .config = REGDMA_LINK_ADDR_MAP_INIT(REGDMA_H264_LINK(pri), \
                                            H264_DMA_IN_CH_BASE(i), H264_DMA_IN_CH_BASE(i), \
                                            H264_DMA_IN_CH_REGS_CNT, 0, 0, \
                                            h264_dma_in_ch_map[0], h264_dma_in_ch_map[1], \
                                            h264_dma_in_ch_map[2], h264_dma_in_ch_map[3]), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_DMA_FIRST_IN5_GLOB \
    [10] = { \
        .config = REGDMA_LINK_ADDR_MAP_INIT(REGDMA_H264_LINK(0x0b), \
                                            H264_DMA_IN_CH5_BASE, H264_DMA_IN_CH5_BASE, \
                                            H264_DMA_IN_CH5_REGS_CNT, 0, 0, \
                                            h264_dma_in_ch5_map[0], h264_dma_in_ch5_map[1], \
                                            h264_dma_in_ch5_map[2], h264_dma_in_ch5_map[3]), \
        .owner = ENTRY(0) | ENTRY(2), \
    }, \
    [11] = { \
        .config = REGDMA_LINK_ADDR_MAP_INIT(REGDMA_H264_LINK(0x0c), \
                                            H264_DMA_GLOBAL_BASE, H264_DMA_GLOBAL_BASE, \
                                            H264_DMA_GLOBAL_REGS_CNT, 0, 0, \
                                            h264_dma_global_map[0], h264_dma_global_map[1], \
                                            h264_dma_global_map[2], h264_dma_global_map[3]), \
        .owner = ENTRY(0) | ENTRY(2), \
    }

/* Core continuous links start at H264_CORE_IDX0 after DMA links (see pbyte link when rev≥300). */

#define H264_EMIT_DMA_OUT5(m) \
    m(0, 0x01), m(1, 0x02), m(2, 0x03), m(3, 0x04), m(4, 0x05)
#define H264_EMIT_DMA_IN5(m) \
    m(0, 0x06), m(1, 0x07), m(2, 0x08), m(3, 0x09), m(4, 0x0a)

#define H264_ENTRY_A1(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x00), \
                                              (void *)(uintptr_t)H264_CORE_BASE, \
                                              (void *)(uintptr_t)H264_CORE_BASE, \
                                              H264_CORE_HEAD_A1_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_ENTRY_A2(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x0e), \
                                              (void *)(uintptr_t)H264_CORE_HEAD_A2_BASE, \
                                              (void *)(uintptr_t)H264_CORE_HEAD_A2_BASE, \
                                              H264_CORE_HEAD_A2_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_ENTRY_B1A(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x0d), \
                                              (void *)(uintptr_t)H264_CORE_HEAD_B1A_BASE, \
                                              (void *)(uintptr_t)H264_CORE_HEAD_B1A_BASE, \
                                              H264_CORE_HEAD_B1A_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_ENTRY_B1B(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x12), \
                                              (void *)(uintptr_t)H264_CORE_HEAD_B1B_BASE, \
                                              (void *)(uintptr_t)H264_CORE_HEAD_B1B_BASE, \
                                              H264_CORE_HEAD_B1B_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_ENTRY_B2(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x0f), \
                                              (void *)(uintptr_t)H264_CORE_HEAD_B2_BASE, \
                                              (void *)(uintptr_t)H264_CORE_HEAD_B2_BASE, \
                                              H264_CORE_HEAD_B2_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#define H264_ENTRY_MID(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x10), \
                                              (void *)(uintptr_t)H264_CORE_MID_BASE, \
                                              (void *)(uintptr_t)H264_CORE_MID_BASE, \
                                              H264_CORE_MID_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#if HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300
#define H264_ENTRY_TAIL_LINE \
    H264_ENTRY_TAIL(H264_CORE_IDX0 + 6),
#define H264_ENTRY_TAIL(n) \
    [(n)] = { \
        .config = REGDMA_LINK_CONTINUOUS_INIT(REGDMA_H264_LINK(0x11), \
                                              (void *)(uintptr_t)H264_CORE_TAIL_BASE, \
                                              (void *)(uintptr_t)H264_CORE_TAIL_BASE, \
                                              H264_CORE_TAIL_WORDS, \
                                              0, \
                                              0), \
        .owner = ENTRY(0) | ENTRY(2), \
    }
#else
#define H264_ENTRY_TAIL_LINE
#endif /* HAL_CONFIG(CHIP_SUPPORT_MIN_REV) >= 300 */

static const regdma_entries_config_t h264_regdma_entries[] = {
    H264_EMIT_DMA_OUT5(H264_DMA_FIRST_OUT),
    H264_EMIT_DMA_IN5(H264_DMA_FIRST_IN),
    H264_DMA_FIRST_IN5_GLOB,
    H264_DMA_LINK_PBYTE
    H264_ENTRY_A1(H264_CORE_IDX0 + 0),
    H264_ENTRY_A2(H264_CORE_IDX0 + 1),
    H264_ENTRY_B1A(H264_CORE_IDX0 + 2),
    H264_ENTRY_B1B(H264_CORE_IDX0 + 3),
    H264_ENTRY_B2(H264_CORE_IDX0 + 4),
    H264_ENTRY_MID(H264_CORE_IDX0 + 5),
    H264_ENTRY_TAIL_LINE
};

const h264_reg_ctx_link_t h264_regs_retention = {
    .link_list = h264_regdma_entries,
    .link_num = ARRAY_SIZE(h264_regdma_entries),
    .module_id = SLEEP_RETENTION_MODULE_H264,
};

#endif /* CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP */
