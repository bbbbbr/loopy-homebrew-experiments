#include "loopy.h"
#include "loopy_helpers.h"

#include "gbdk/platform.h"

#include "serial.h"
#include "music_data.h"

#include "parrots_rgb444.h"
#include "gradients_rgb444.h"

#define TILE_NUM_0  0u


bool gamepadActive = false;
bool mouseControlMode = false;
uint32_t pressedButtons = 0;
uint32_t heldButtons = 0;

struct soundstate_t soundState;

void setupSystem() {
	// Turn off all video output
	VDP.SCREENPRIO = 0;

    // Set Backdrop color to RGB 0,0,0
	VDP.BACKDROP_A = RGB888(0,0,0);
	VDP.BACKDROP_B = RGB888(0,0,0);

	// Turn off controller input
	bios_vdpMode(CONTROL_MODE_NONE, 0);

	// Setup sound hardware (takes a few frames)
	bios_soundChannels(SOUND_CHANS_4CH);
	bios_soundVolume(SOUND_VOL_CH2_3, SOUND_VOL_100);
	bios_soundVolume(SOUND_VOL_CH4,   SOUND_VOL_100);
	bios_initSoundTransmission();

	// Check mouse presence and set control mode accordingly
	// Requires that controller input was turned off for the last few frames
	mouseControlMode = (MOUSE_DET != 0);

	// Enable interrupts and DMA for music
	sys_setInterruptPriority(INT_PRIO_ITU0, 0xF);
	sys_setInterruptMask(0xE);
	sys_setDmaEnabled(true);
}

void init_gfx() {
	bios_vsync();

	VDP.BACKDROP_A      = RGB888(0,0,0);
	VDP.BACKDROP_B      = RGB888(0,0,0);

    // Blend style
    VDP.BLEND           = BLEND_MATH;

    for (int c = 0; c < 4; c++) {
        VDP.BM_SCREENX[c]   = 0u;
        VDP.BM_SCREENY[c]   = 0u;
        VDP.BM_WIDTH[c]     = (0u << 8) | 255u;
        VDP.BM_HEIGHT[c]    = 223u;
    }
    // 256x512 Bitmap layer
    //
    //   Red + Green(bm0)
    //   ----------------
    //       Blue(bm2)
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

void load_rgb444_256x224_image(const uint8_t * p_src_img_4bpp) {

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

            // Now using `-loopy_hicol_rgb444` preset with conversion tool png2reducedrgb
            // which handles all the data formatting ahead of time
            //
            // Might optimized further as separate blocks for line based DMA
            p_bm[BLUE444_OFFSET] = *p_src_img_4bpp++;
            p_bm[REDGREEN444_OFFSET] = *p_src_img_4bpp++;

            // Previous manual formatting version
            //
            // // Blue Channel
            // p_bm[BLUE444_OFFSET]  = PALBLUE_OFFSET + (uint8_t)(pixel444 & 0x0Fu);
            //
            // // Red + Green Channels
            // uint16_t green = (pixel444 >> 4) & 0x0Fu;
            // uint16_t red   = (pixel444 >> 8) & 0x0Fu;
            //          if (red > 0) red--;  // Remap red 1-15 -> 0-14 to deal with only 14 palette range for it
            // p_bm[REDGREEN444_OFFSET]  = (red << 4) | green;

            p_bm++; // Next bitmap pixel
        }
    }
}


int main() {
	// Initialize the hardware and system state, and check controller mode
	setupSystem();

	// // Print a hellorld message on the serial port
	// serial_begin(9600);
	// serial_print("Casio Loopy says hello world\r\n");

	// Set the appropriate controller scanning mode and video height
	bios_vdpMode(mouseControlMode ? CONTROL_MODE_MOUSE : CONTROL_MODE_GAMEPAD, VIDEO_HEIGHT_224P);

	// Display some graphics on the BM0 layer and set a backdrop
	init_gfx();


    // load_rgb444_256x224_image(parrots_rgb444);
    load_rgb444_256x224_image(gradients_rgb444);

    bool showGradientImage = true;
	while(1) {
		bios_vsync();

        // Update button state from gamepad buttons
        uint32_t buttonsNow = READ_GAMEPAD1;
        pressedButtons = buttonsNow & ~heldButtons;
        heldButtons = buttonsNow;

        if (pressedButtons & GAMEPAD_BTN_START) {
            showGradientImage = !showGradientImage;
            if (showGradientImage) load_rgb444_256x224_image(gradients_rgb444);
            else                   load_rgb444_256x224_image(parrots_rgb444);
        }

        if (pressedButtons & GAMEPAD_BTN_A) {
            VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_B, LAYER_SCREEN_A, LAYER_SCREEN_A) | LAYER_ENABLE_BM0;
        } else if (pressedButtons & GAMEPAD_BTN_B) {
            VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_B, LAYER_SCREEN_A, LAYER_SCREEN_A) |  LAYER_ENABLE_BM2;
        } else if (pressedButtons & GAMEPAD_BTN_C) {
            VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_B, LAYER_SCREEN_A, LAYER_SCREEN_A) | LAYER_ENABLE_BM0 | LAYER_ENABLE_BM2;
        }        

    }

	return 0;
}
