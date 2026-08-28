    VDP.BM_SCROLLX[0] = 0u; // Red
    VDP.BM_SCROLLY[0] = 0u;
    VDP.BM_SCROLLX[2] = 0u; // Blue
    VDP.BM_SCROLLY[2] = DEVICE_SCREEN_PX_HEIGHT;
    
    VDP.BM_CTRL         = BM_MODE_8BPP_SHARED; /* 256 x 512, 8bpp */
    VDP.SCREENPRIO      = BLEND_MATH_ADD | SCREEN_A_ENABLE | SCREEN_B_ENABLE | PRIORITY_BM_B | PRIORITY_BG0_A | PRIORITY_OBJ0_A;
    VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_B, LAYER_SCREEN_A, LAYER_SCREEN_A) | LAYER_ENABLE_BM0 | LAYER_ENABLE_BM2;
}


// Load RGB555 image and split into 2 planes: Red+Green(bm0), Blue(bm2)
//
// 256x512 Bitmap layer
//
//   Red + Green(bm0)
//   ----------------
//       Blue(bm2)
//
#define BM_TILEMAP_W 256u
#define REDGREEN444_OFFSET   (0u)
#define BLUE444_OFFSET       (DEVICE_SCREEN_PX_HEIGHT * (BM_TILEMAP_W))

#define PAL_15_BLUE_4BPP  15u
#define PALBLUE_OFFSET   (15u * COLS_PER_PAL_4BPP)

void load_rgb444_256x224_image(const uint16_t * p_src_img_4bpp) {

    // Prepare palettes
    uint16_t * p_pal = &VDP.PALETTE[0];

    // Blue: allocated to last palette 4 bit range 0-30
    for (unsigned int c = 0; c < COLS_PER_PAL_4BPP; c++) {
        *(p_pal + c + (COLS_PER_PAL_4BPP * PAL_15_BLUE_4BPP)) = RGB555(0, 0, c << 1);
    }

    // Red + Green share the first 15 palettes and have ~4 bit range
    // Red:  ~3.5 bit range 2-30 over the span of 14 palettes, mixed with the green ramp per palette
    // Green: 4 bit range 0-30 within each palette
    for (unsigned int red = 0; red < (COLS_PER_PAL_4BPP - 1); red++) {
        for (unsigned int green = 0; green < COLS_PER_PAL_4BPP; green++) {
            *(p_pal + green + (COLS_PER_PAL_4BPP * red)) = RGB555((red+1) << 1, green << 1, 0);
        }
    }

    uint8_t  * p_bm  = VDP.BITMAP_VRAM_8BIT;
    for (uint16_t y = 0; y < DEVICE_SCREEN_PX_HEIGHT; y++) {
        for (uint16_t x = 0; x < DEVICE_SCREEN_PX_WIDTH; x++) {

            uint16_t pixel444  = *p_src_img_4bpp++; // Load image pixel and advance to next

            // Blue Channel
            p_bm[BLUE444_OFFSET]  = PALBLUE_OFFSET + (uint8_t)(pixel444 & 0x0Fu);

            // Red + Green Channels
            uint16_t green = (pixel444 >> 4) & 0x0Fu;
            uint16_t red   = (pixel444 >> 8) & 0x0Fu;
                     if (red > 0) red--;  // Remap red 1-15 -> 0-14 to deal with only 14 palette range for it
            p_bm[REDGREEN444_OFFSET]  = (red << 4) | green;

            p_bm++; // Next bitmap pixel
        }
    }
}

