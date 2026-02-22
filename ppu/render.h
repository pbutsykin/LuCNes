/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2021 Pavel Butsykin
 */
#ifndef __CNES_PPU_RENDER_
#define __CNES_PPU_RENDER_

void PpuVisibleLineRender(LuCNesPPU* ppu);
void PpuPreRenderLine(LuCNesPPU* ppu);

#ifndef NDEBUG
void PpuRenderFrameDebug(LuCNesPPU* const ppu);
#endif

#endif
