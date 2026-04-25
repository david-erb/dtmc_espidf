#pragma once

#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dttasker.h>
#include <dtmc_base/dtuart_helpers.h>

// forward-declare concrete type
typedef struct dtdisplay_lcd_ili9341_t dtdisplay_lcd_ili9341_t;

typedef struct dtdisplay_lcd_ili9341_config_t
{
    char title[64];
    uint32_t window_width;
    uint32_t window_height;
    dtsemaphore_handle join_semaphore;
} dtdisplay_lcd_ili9341_config_t;

extern dterr_t*
dtdisplay_lcd_ili9341_create(dtdisplay_lcd_ili9341_t** self_ptr);

extern dterr_t*
dtdisplay_lcd_ili9341_init(dtdisplay_lcd_ili9341_t* self);

extern dterr_t*
dtdisplay_lcd_ili9341_config(dtdisplay_lcd_ili9341_t* self, const dtdisplay_lcd_ili9341_config_t* cfg);

dterr_t*
dtdisplay_lcd_ili9341_blit(dtdisplay_lcd_ili9341_t* self DTDISPLAY_BLIT_ARGS);
dterr_t*
dtdisplay_lcd_ili9341_attach(dtdisplay_lcd_ili9341_t* self DTDISPLAY_ATTACH_ARGS);
dterr_t*
dtdisplay_lcd_ili9341_detach(dtdisplay_lcd_ili9341_t* self DTDISPLAY_DETACH_ARGS);
dterr_t*
dtdisplay_lcd_ili9341_create_compatible_raster(dtdisplay_lcd_ili9341_t* self DTRASTER_CREATE_COMPATIBLE_RASTER_ARGS);

// -----------------------------------------------------------------------------
// Interface plumbing.

DTIOX_DECLARE_API(dtdisplay_lcd_ili9341);
DTOBJECT_DECLARE_API(dtdisplay_lcd_ili9341);
