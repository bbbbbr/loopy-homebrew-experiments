#include "loopy.h"
#include "loopy_helpers.h"

#include "gbdk/platform.h"

#include "serial.h"
#include "music_data.h"

#include "rgb888_range_test.h"
#include "parrots.h"

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

    // 512x512 Bitmap layer
    //
    //   Red(bm0)  | Green(bm1)
    //   ----------------------
    //   Blue(bm2) | LSBits(gm3)    
    VDP.BM_SCROLLX[0] = 0u; // Red
    VDP.BM_SCROLLY[0] = 0u;
        // VDP.BM_SCREENX[0]   = 32u;
        // VDP.BM_SCREENY[0]   = 32u;

    VDP.BM_SCROLLX[1] = DEVICE_SCREEN_PX_WIDTH; // Green
    VDP.BM_SCROLLY[1] = 0u;

    VDP.BM_SCROLLX[2] = 0u; // Blue
    VDP.BM_SCROLLY[2] = DEVICE_SCREEN_PX_HEIGHT;
        // VDP.BM_SCREENX[2]   = 64u;
        // VDP.BM_SCREENY[2]   = 64u;

    VDP.BM_SCROLLX[3] = DEVICE_SCREEN_PX_WIDTH; // LSBits
    VDP.BM_SCROLLY[3] = DEVICE_SCREEN_PX_HEIGHT;

    VDP.BM_SUBPAL       = BM_SUBPAL(0,1,2,3);
    VDP.BM_CTRL         = BM_MODE_4BPP_SHARED;
    VDP.SCREENPRIO      = BLEND_MATH_ADD | SCREEN_A_ENABLE | SCREEN_B_ENABLE | PRIORITY_BM_B | PRIORITY_BG0_A | PRIORITY_OBJ0_A;
    // (all bm* on A, no blending)
    // VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_A, LAYER_SCREEN_A, LAYER_SCREEN_A) | LAYER_ENABLE_BM0 | LAYER_ENABLE_BM1 | LAYER_ENABLE_BM2 | LAYER_ENABLE_BM3;
// BM0 and 2 only, Blend ON
VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_B, LAYER_SCREEN_A, LAYER_SCREEN_A) | LAYER_ENABLE_BM0 | LAYER_ENABLE_BM2;
    // (bm01 on A, bm12 on B, blending)
    // VDP.LAYER_CTRL      = LAYER_SCREEN(LAYER_SCREEN_A, LAYER_SCREEN_B, LAYER_SCREEN_A, LAYER_SCREEN_A) | LAYER_ENABLE_BM0 | LAYER_ENABLE_BM1 | LAYER_ENABLE_BM2 | LAYER_ENABLE_BM3;
}

// Test: Draw onto BG1 (and offset loaded palette, tiles and tilemap entries)
void load_rgb555_image(void) {

    // Prepare palettes
    uint16_t * p_pal = &VDP.PALETTE[0];

    // Set up RGB channel palettes for BM0/1/2
    const uint8_t pal444[COLS_PER_PAL_4BPP] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30};
    const uint16_t palrgb_lsbits[COLS_PER_PAL_4BPP] = {
        RGB555(0,0,0), RGB555(1,0,0), RGB555(0,1,0),    // 0:Black, 1:Blue LSBit, 2:Green LSBit
        RGB555(1,1,0), RGB555(0,0,1), RGB555(1,0,1),    // 3:Blue+Green LSBits, 4:Red LSBit, 5:Red + Blue LSBit
        RGB555(0,1,1), RGB555(1,1,1), 0,0,0,0,0,0,0,0}; // 6:Red+Green LSBits, 7:Red+Green+Blue LSBits, 8..15: Black


    for (unsigned int c = 0; c < COLS_PER_PAL_4BPP; c++) {
        *p_pal                             = RGB555(pal444[c], 0, 0); // Red
        *(p_pal +  COLS_PER_PAL_4BPP)      = RGB555(0, pal444[c], 0); // Green
        *(p_pal + (COLS_PER_PAL_4BPP * 2)) = RGB555(0, 0, pal444[c]); // Blue
        // *(p_pal + (COLS_PER_PAL_4BPP * 3)) = palrgb_lsbits[c]; // RGBLSBits
        p_pal++;
    }

    // Load RGB555 image and split into 4 planes: R4(bm0), G4(bm1), B4(bm2), LSBits(bm3)
    // The /2 is due to 4bpp mode packing 2 pixels into each byte
    #define BM_TILEMAP_W 512u    
    #define ROWSTRIDE ((BM_TILEMAP_W / 2) - ((DEVICE_SCREEN_PX_WIDTH / 2)))

    // 512x512 Bitmap layer
    //
    //   Red(bm0)  | Green(bm1)
    //   ----------------------
    //   Blue(bm2) | LSBits(gm3)
    // The /2 is due to 4bpp mode packing 2 pixels into each byte
    #define RED444_OFFSET   (0u)                                    
    #define GREEN444_OFFSET (DEVICE_SCREEN_PX_WIDTH / 2)
    #define BLUE444_OFFSET  (DEVICE_SCREEN_PX_HEIGHT * (BM_TILEMAP_W / 2))
    #define LSBITS_OFFSET   (DEVICE_SCREEN_PX_HEIGHT * (BM_TILEMAP_W / 2) + (DEVICE_SCREEN_PX_WIDTH / 2))

    // const uint16_t * p_img = rgb888_range_test;
    const uint16_t * p_img = parrots;
          uint8_t  * p_bm  = VDP.BITMAP_VRAM_8BIT;

    for (uint16_t y = 0; y < DEVICE_SCREEN_PX_HEIGHT; y++) {
        for (uint16_t x = 0; x < DEVICE_SCREEN_PX_WIDTH; x+=2) {


/*
            Test gradients on BM0/1/2 - works
            p_bm[RED444_OFFSET]  = (((y/16) & 0x1F) << 4) | ((y /16) & 0x1F);
            p_bm[GREEN444_OFFSET] = ((15-(x/16 & 0x1F)) << 4) | ((15-((x+1)/16)) & 0x1F);
            p_bm[BLUE444_OFFSET]   = ((x/16 & 0x1F) << 4) | (((x+1)/16) & 0x1F);
            // p_bm[LSBITS_OFFSET]   = 0;
*/
            // Test image, works with ONLY 2 channels at once
            // due to all colors in given bm being same channel
            // and blending only between *screens* and not bm*s

            uint16_t left_pixel555  = *p_img++; // Load image pixel and advance to next
            uint16_t right_pixel555 = *p_img++; // Load image pixel and advance to next

            // Blue
            uint8_t lsbits   =  (uint8_t) left_pixel555 & 0x01u << 4; // Save Blue LSBit
                    lsbits   |= (uint8_t)right_pixel555 & 0x01u;
            left_pixel555  >>= 1;
            right_pixel555 >>= 1;
            p_bm[BLUE444_OFFSET]  = ((uint8_t)left_pixel555 & 0x0Fu) << 4 | ((uint8_t)right_pixel555 & 0x0Fu);
            left_pixel555  >>= 4;
            right_pixel555 >>= 4;

            // Green
            lsbits |= ((uint8_t) left_pixel555 & 0x01u) << (1 + 4); // Save Green LSBit
            lsbits |= ((uint8_t)right_pixel555 & 0x01u) << 1;
            left_pixel555  >>= 1;
            right_pixel555 >>= 1;
            p_bm[GREEN444_OFFSET]  = ((uint8_t)left_pixel555 & 0x0Fu) << 4 | ((uint8_t)right_pixel555 & 0x0Fu);
            left_pixel555  >>= 4;
            right_pixel555 >>= 4;

            // Red
            lsbits |= ((uint8_t) left_pixel555 & 0x01u) << (2 + 4); // Save Red LSBit
            lsbits |= ((uint8_t)right_pixel555 & 0x01u) << 2;
            left_pixel555  >>= 1;
            right_pixel555 >>= 1;
            p_bm[RED444_OFFSET]  = ((uint8_t)left_pixel555 & 0x0Fu) << 4 | ((uint8_t)right_pixel555 & 0x0Fu);

            // LSBits
            p_bm[LSBITS_OFFSET]  = lsbits;

            p_bm++; // Next bitmap pixel pair
        }
        p_bm += ROWSTRIDE; // Next bitmap row
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


    // Test:
    load_rgb555_image();

	// Every frame, move the BM0 layer according to gamepad/mouse
	while(1) {
		bios_vsync();

            // Update button state from gamepad buttons
            uint32_t buttonsNow = READ_GAMEPAD1;
            pressedButtons = buttonsNow & ~heldButtons;
            heldButtons = buttonsNow;

            // Set the motion from D-pad directions
            if (heldButtons & GAMEPAD_BTN_LEFT) {
                VDP.BM_SCROLLX[1]--;
            } else if (heldButtons & GAMEPAD_BTN_RIGHT) {
                VDP.BM_SCROLLX[1]++;
            }

            if(heldButtons & GAMEPAD_BTN_UP) {
                VDP.BM_SCROLLY[1]--;
            } else if(heldButtons & GAMEPAD_BTN_DOWN) {
                VDP.BM_SCROLLY[1]++;
            }        

            // Set the motion from D-pad directions
            if (heldButtons & GAMEPAD_BTN_RTRIG) {
                VDP.BM_SCROLLX[2]--;
            } else if (heldButtons & GAMEPAD_BTN_LTRIG) {
                VDP.BM_SCROLLX[2]++;
            }

            if(heldButtons & GAMEPAD_BTN_B) {
                VDP.BM_SCROLLY[2]--;
            } else if(heldButtons & GAMEPAD_BTN_A) {
                VDP.BM_SCROLLY[2]++;
            }        
	}

	return 0;
}
