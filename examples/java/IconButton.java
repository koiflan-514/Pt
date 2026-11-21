/*
 * Pt PNG icon buttons: a reference-counted raster image decoded from a
 * MemorySegment, shared by three buttons, tinted, and set as the window icon.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (src/pt/Pt.java, include/pt/icon.h):
 *
 *  - icon_image_load_memory copies PNG bytes from a MemorySegment and returns
 *    one owned reference; width/height read the decoded pixels.
 *  - icon_image_ref adds a reference and returns the same handle. Button and
 *    window slots take their own reference, so the caller can drop its handle
 *    while the slots keep the pixels alive.
 *  - button_set_icon_tint(0) restores the image's own colours; any other
 *    0xRRGGBBAA value replaces RGB with the tint scaled by alpha.
 *  - window_set_icon is best effort; the getter still reports the retained
 *    image on compositors without xdg_toplevel_icon_v1 and under
 *    PT_HEADLESS.
 *
 * The 32x32 PNG below is embedded so the example needs no asset file: a warm
 * ring with a cross bar and a centre dot on a transparent background.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import pt.Pt;

public final class IconButton {
    private IconButton() {}

    private static final byte[] ICON_PNG = {
        (byte) 0x89, (byte) 0x50, (byte) 0x4E, (byte) 0x47, (byte) 0x0D, (byte) 0x0A, (byte) 0x1A, (byte) 0x0A, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x0D,
        (byte) 0x49, (byte) 0x48, (byte) 0x44, (byte) 0x52, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x20, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x20,
        (byte) 0x08, (byte) 0x06, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x73, (byte) 0x7A, (byte) 0x7A, (byte) 0xF4, (byte) 0x00, (byte) 0x00, (byte) 0x03,
        (byte) 0xBA, (byte) 0x49, (byte) 0x44, (byte) 0x41, (byte) 0x54, (byte) 0x78, (byte) 0xDA, (byte) 0xED, (byte) 0xD7, (byte) 0x5B, (byte) 0x6B, (byte) 0xDB,
        (byte) 0x30, (byte) 0x18, (byte) 0xC6, (byte) 0xF1, (byte) 0x7D, (byte) 0x9D, (byte) 0x9E, (byte) 0xD7, (byte) 0x6D, (byte) 0xDD, (byte) 0xA1, (byte) 0xEB,
        (byte) 0xDA, (byte) 0x75, (byte) 0xDB, (byte) 0xD7, (byte) 0x0A, (byte) 0x18, (byte) 0x0C, (byte) 0x02, (byte) 0x83, (byte) 0xC1, (byte) 0x10, (byte) 0x30,
        (byte) 0x01, (byte) 0x83, (byte) 0x41, (byte) 0x20, (byte) 0x10, (byte) 0x08, (byte) 0x0C, (byte) 0x86, (byte) 0x60, (byte) 0x08, (byte) 0x18, (byte) 0x0C,
        (byte) 0x06, (byte) 0x81, (byte) 0xC1, (byte) 0x90, (byte) 0xEF, (byte) 0x91, (byte) 0xA6, (byte) 0x69, (byte) 0xD7, (byte) 0xAD, (byte) 0xA7, (byte) 0xED,
        (byte) 0x4E, (byte) 0xFB, (byte) 0xDF, (byte) 0x35, (byte) 0xB0, (byte) 0xB4, (byte) 0x4D, (byte) 0x5B, (byte) 0xF7, (byte) 0x66, (byte) 0x4C, (byte) 0xF0,
        (byte) 0xDE, (byte) 0x25, (byte) 0xAF, (byte) 0x7F, (byte) 0x24, (byte) 0x7A, (byte) 0x1F, (byte) 0xC9, (byte) 0x2F, (byte) 0x5E, (byte) 0xFC, (byte) 0x5F,
        (byte) 0x0F, (byte) 0x5C, (byte) 0xA7, (byte) 0xBD, (byte) 0x5D, (byte) 0xF7, (byte) 0xBD, (byte) 0xF7, (byte) 0xC1, (byte) 0x9D, (byte) 0xF5, (byte) 0xDE,
        (byte) 0x53, (byte) 0xEF, (byte) 0xDC, (byte) 0x8F, (byte) 0xDE, (byte) 0x5B, (byte) 0x6A, (byte) 0xC7, (byte) 0xFD, (byte) 0xEC, (byte) 0xBD, (byte) 0x71,
        (byte) 0xE7, (byte) 0xBD, (byte) 0xD7, (byte) 0xEE, (byte) 0xD9, (byte) 0x1E, (byte) 0x7C, (byte) 0xE2, (byte) 0xED, (byte) 0xB9, (byte) 0x53, (byte) 0xEF,
        (byte) 0x23, (byte) 0x05, (byte) 0xC0, (byte) 0x03, (byte) 0xE0, (byte) 0x01, (byte) 0xF0, (byte) 0x00, (byte) 0x78, (byte) 0x00, (byte) 0x3C, (byte) 0x00,
        (byte) 0x1E, (byte) 0x00, (byte) 0xEF, (byte) 0x35, (byte) 0xF5, (byte) 0xCA, (byte) 0x5D, (byte) 0x78, (byte) 0xDB, (byte) 0xDD, (byte) 0x41, (byte) 0x66,
        (byte) 0xFE, (byte) 0xBE, (byte) 0x9B, (byte) 0xF9, (byte) 0x9F, (byte) 0xDC, (byte) 0x89, (byte) 0x0F, (byte) 0xC0, (byte) 0x07, (byte) 0xE0, (byte) 0x03,
        (byte) 0xF0, (byte) 0x01, (byte) 0xF8, (byte) 0x00, (byte) 0x7C, (byte) 0x00, (byte) 0x3E, (byte) 0x00, (byte) 0x1F, (byte) 0x80, (byte) 0x0F, (byte) 0xC0,
        (byte) 0x07, (byte) 0xE0, (byte) 0x03, (byte) 0xF0, (byte) 0xB7, (byte) 0xDD, (byte) 0xA5, (byte) 0xFF, (byte) 0x92, (byte) 0xDA, (byte) 0x7A, (byte) 0x1A,
        (byte) 0xE4, (byte) 0x58, (byte) 0x1C, (byte) 0xB8, (byte) 0x99, (byte) 0x00, (byte) 0x20, (byte) 0x00, (byte) 0x08, (byte) 0x00, (byte) 0x02, (byte) 0x80,
        (byte) 0x00, (byte) 0x20, (byte) 0x00, (byte) 0x08, (byte) 0x00, (byte) 0x02, (byte) 0x80, (byte) 0x00, (byte) 0x20, (byte) 0x00, (byte) 0x08, (byte) 0x00,
        (byte) 0x02, (byte) 0x80, (byte) 0x00, (byte) 0x20, (byte) 0x00, (byte) 0x08, (byte) 0x00, (byte) 0x62, (byte) 0xCB, (byte) 0x5D, (byte) 0x89, (byte) 0xCD,
        (byte) 0xC7, (byte) 0x21, (byte) 0xA6, (byte) 0xC1, (byte) 0x67, (byte) 0x77, (byte) 0x1C, (byte) 0x00, (byte) 0x08, (byte) 0x00, (byte) 0x04, (byte) 0x00,
        (byte) 0x02, (byte) 0x00, (byte) 0x01, (byte) 0x80, (byte) 0x60, (byte) 0xF7, (byte) 0xD6, (byte) 0x86, (byte) 0xE7, (byte) 0x01, (byte) 0x80, (byte) 0x00,
        (byte) 0x40, (byte) 0x00, (byte) 0x20, (byte) 0x00, (byte) 0x10, (byte) 0x00, (byte) 0x08, (byte) 0x36, (byte) 0xDD, (byte) 0x75, (byte) 0xB0, (byte) 0xF1,
        (byte) 0x30, (byte) 0xC4, (byte) 0x34, (byte) 0x3C, (byte) 0x74, (byte) 0xD3, (byte) 0x10, (byte) 0x40, (byte) 0x08, (byte) 0x20, (byte) 0x04, (byte) 0x10,
        (byte) 0x02, (byte) 0x08, (byte) 0xF7, (byte) 0x96, (byte) 0x6E, (byte) 0x72, (byte) 0x11, (byte) 0x02, (byte) 0x08, (byte) 0x01, (byte) 0x84, (byte) 0x00,
        (byte) 0x42, (byte) 0x00, (byte) 0xE1, (byte) 0x06, (byte) 0xB5, (byte) 0xBE, (byte) 0xDC, (byte) 0xF7, (byte) 0x8F, (byte) 0xA2, (byte) 0x2F, (byte) 0x6E,
        (byte) 0x1A, (byte) 0x01, (byte) 0x88, (byte) 0x00, (byte) 0x44, (byte) 0x00, (byte) 0xA2, (byte) 0xFD, (byte) 0x47, (byte) 0xFF, (byte) 0x8F, (byte) 0x97,
        (byte) 0x11, (byte) 0x80, (byte) 0x08, (byte) 0x40, (byte) 0x04, (byte) 0x20, (byte) 0x5A, (byte) 0x77, (byte) 0xBF, (byte) 0xA2, (byte) 0xB5, (byte) 0xFB,
        (byte) 0x7B, (byte) 0x1D, (byte) 0xF5, (byte) 0x01, (byte) 0xF4, (byte) 0x01, (byte) 0xF4, (byte) 0x01, (byte) 0xF4, (byte) 0x0F, (byte) 0x9E, (byte) 0xBC,
        (byte) 0x9B, (byte) 0xAF, (byte) 0xFA, (byte) 0x00, (byte) 0xFA, (byte) 0x00, (byte) 0xFA, (byte) 0x00, (byte) 0xFA, (byte) 0xF7, (byte) 0x00, (byte) 0x26,
        (byte) 0xF1, (byte) 0x57, (byte) 0x77, (byte) 0x14, (byte) 0x03, (byte) 0x88, (byte) 0x01, (byte) 0xC4, (byte) 0x00, (byte) 0xE2, (byte) 0x0E, (byte) 0x00,
        (byte) 0x31, (byte) 0x80, (byte) 0x18, (byte) 0x40, (byte) 0x0C, (byte) 0x20, (byte) 0x5E, (byte) 0x73, (byte) 0xBF, (byte) 0xE3, (byte) 0xD5, (byte) 0xDB,
        (byte) 0x7B, (byte) 0x4E, (byte) 0x06, (byte) 0x00, (byte) 0x06, (byte) 0x00, (byte) 0x06, (byte) 0x00, (byte) 0x06, (byte) 0x9F, (byte) 0x3B, (byte) 0x9B,
        (byte) 0xE5, (byte) 0xEB, (byte) 0x01, (byte) 0x80, (byte) 0x01, (byte) 0x80, (byte) 0x01, (byte) 0x80, (byte) 0xC1, (byte) 0x2D, (byte) 0x80, (byte) 0x49,
        (byte) 0xF2, (byte) 0xCD, (byte) 0x4D, (byte) 0x12, (byte) 0x00, (byte) 0x09, (byte) 0x80, (byte) 0x04, (byte) 0x40, (byte) 0xD2, (byte) 0x21, (byte) 0x20,
        (byte) 0x01, (byte) 0x90, (byte) 0x00, (byte) 0x48, (byte) 0x00, (byte) 0x24, (byte) 0xAB, (byte) 0xD4, (byte) 0xCA, (byte) 0xDF, (byte) 0xBD, (byte) 0x27,
        (byte) 0x29, (byte) 0x80, (byte) 0x14, (byte) 0x40, (byte) 0x0A, (byte) 0x20, (byte) 0x3D, (byte) 0xBC, (byte) 0xF7, (byte) 0xE1, (byte) 0x67, (byte) 0x29,
        (byte) 0x39, (byte) 0x90, (byte) 0x92, (byte) 0x03, (byte) 0x29, (byte) 0x39, (byte) 0x90, (byte) 0xEE, (byte) 0xDC, (byte) 0xFB, (byte) 0xF9, (byte) 0xEB,
        (byte) 0x14, (byte) 0x40, (byte) 0x0A, (byte) 0x20, (byte) 0x05, (byte) 0x90, (byte) 0x2E, (byte) 0x02, (byte) 0x48, (byte) 0x00, (byte) 0x12, (byte) 0x80,
        (byte) 0x04, (byte) 0x20, (byte) 0x6F, (byte) 0x00, (byte) 0x33, (byte) 0xC9, (byte) 0x18, (byte) 0x4A, (byte) 0xC6, (byte) 0x50, (byte) 0x92, (byte) 0x03,
        (byte) 0x92, (byte) 0x1C, (byte) 0x90, (byte) 0x04, (byte) 0x91, (byte) 0x24, (byte) 0x88, (byte) 0x24, (byte) 0x00, (byte) 0x09, (byte) 0x40, (byte) 0x02,
        (byte) 0x90, (byte) 0x04, (byte) 0x91, (byte) 0x24, (byte) 0x88, (byte) 0x24, (byte) 0x39, (byte) 0x20, (byte) 0xC9, (byte) 0x01, (byte) 0xC9, (byte) 0x18,
        (byte) 0x4A, (byte) 0xC6, (byte) 0x50, (byte) 0xDE, (byte) 0x24, (byte) 0xE1, (byte) 0xB5, (byte) 0x04, (byte) 0x20, (byte) 0x01, (byte) 0x48, (byte) 0x00,
        (byte) 0x72, (byte) 0x11, (byte) 0x40, (byte) 0x01, (byte) 0x50, (byte) 0x00, (byte) 0x14, (byte) 0x00, (byte) 0x35, (byte) 0x07, (byte) 0x50, (byte) 0x00,
        (byte) 0x14, (byte) 0x00, (byte) 0x05, (byte) 0x40, (byte) 0x01, (byte) 0x50, (byte) 0x00, (byte) 0x14, (byte) 0x00, (byte) 0x05, (byte) 0x40, (byte) 0x01,
        (byte) 0x50, (byte) 0x00, (byte) 0x14, (byte) 0x00, (byte) 0x05, (byte) 0x40, (byte) 0x01, (byte) 0x50, (byte) 0x00, (byte) 0x14, (byte) 0x00, (byte) 0x05,
        (byte) 0x40, (byte) 0xCD, (byte) 0x01, (byte) 0x14, (byte) 0x00, (byte) 0x05, (byte) 0x40, (byte) 0x01, (byte) 0x50, (byte) 0x8B, (byte) 0x00, (byte) 0x1A,
        (byte) 0x80, (byte) 0x06, (byte) 0xA0, (byte) 0x01, (byte) 0xE8, (byte) 0x39, (byte) 0x80, (byte) 0x06, (byte) 0xA0, (byte) 0x01, (byte) 0x68, (byte) 0x00,
        (byte) 0x1A, (byte) 0x80, (byte) 0x06, (byte) 0xA0, (byte) 0x01, (byte) 0x68, (byte) 0x00, (byte) 0x1A, (byte) 0x80, (byte) 0x06, (byte) 0xA0, (byte) 0x01,
        (byte) 0x68, (byte) 0x00, (byte) 0x1A, (byte) 0x80, (byte) 0x06, (byte) 0xA0, (byte) 0x01, (byte) 0x68, (byte) 0x00, (byte) 0x7A, (byte) 0x0E, (byte) 0xA0,
        (byte) 0x01, (byte) 0x68, (byte) 0x00, (byte) 0x1A, (byte) 0x80, (byte) 0x5E, (byte) 0x04, (byte) 0x30, (byte) 0x00, (byte) 0x0C, (byte) 0x00, (byte) 0x03,
        (byte) 0xC0, (byte) 0xCC, (byte) 0x01, (byte) 0x0C, (byte) 0x00, (byte) 0x03, (byte) 0xC0, (byte) 0x00, (byte) 0x30, (byte) 0x00, (byte) 0x0C, (byte) 0x00,
        (byte) 0x03, (byte) 0xC0, (byte) 0x00, (byte) 0x30, (byte) 0x00, (byte) 0x0C, (byte) 0x00, (byte) 0x03, (byte) 0xC0, (byte) 0x00, (byte) 0x30, (byte) 0x00,
        (byte) 0x0C, (byte) 0x00, (byte) 0x03, (byte) 0xC0, (byte) 0x00, (byte) 0x30, (byte) 0x73, (byte) 0x00, (byte) 0x03, (byte) 0xC0, (byte) 0x00, (byte) 0x30,
        (byte) 0x00, (byte) 0xCC, (byte) 0x22, (byte) 0x40, (byte) 0x06, (byte) 0x20, (byte) 0x03, (byte) 0x90, (byte) 0x01, (byte) 0xC8, (byte) 0x96, (byte) 0xD8,
        (byte) 0x84, (byte) 0x19, (byte) 0x80, (byte) 0x0C, (byte) 0x40, (byte) 0x06, (byte) 0x20, (byte) 0x5B, (byte) 0x62, (byte) 0x13, (byte) 0x66, (byte) 0x00,
        (byte) 0x32, (byte) 0x00, (byte) 0x19, (byte) 0x80, (byte) 0x6C, (byte) 0x11, (byte) 0x20, (byte) 0x07, (byte) 0x90, (byte) 0x03, (byte) 0xC8, (byte) 0x01,
        (byte) 0xE4, (byte) 0x8C, (byte) 0x61, (byte) 0xDE, (byte) 0xE1, (byte) 0x18, (byte) 0xE6, (byte) 0x8C, (byte) 0x61, (byte) 0x0E, (byte) 0x20, (byte) 0x07,
        (byte) 0x90, (byte) 0x03, (byte) 0xC8, (byte) 0x57, (byte) 0x6E, (byte) 0xC9, (byte) 0x82, (byte) 0x21, (byte) 0x80, (byte) 0x21, (byte) 0x80, (byte) 0x21,
        (byte) 0x80, (byte) 0x61, (byte) 0x87, (byte) 0x80, (byte) 0x21, (byte) 0x80, (byte) 0x21, (byte) 0x80, (byte) 0x21, (byte) 0x80, (byte) 0xE1, (byte) 0x5D,
        (byte) 0x49, (byte) 0x58, (byte) 0x00, (byte) 0x28, (byte) 0x00, (byte) 0x14, (byte) 0x00, (byte) 0x0A, (byte) 0xA2, (byte) 0xB8, (byte) 0xE8, (byte) 0x20,
        (byte) 0x8A, (byte) 0x0B, (byte) 0xA2, (byte) 0xB8, (byte) 0x00, (byte) 0x50, (byte) 0x00, (byte) 0x28, (byte) 0x00, (byte) 0x14, (byte) 0xAB, (byte) 0x77,
        (byte) 0xF7, (byte) 0x3C, (byte) 0x1A, (byte) 0x01, (byte) 0x18, (byte) 0x01, (byte) 0x18, (byte) 0x01, (byte) 0x18, (byte) 0x75, (byte) 0x00, (byte) 0x18,
        (byte) 0x01, (byte) 0x18, (byte) 0x01, (byte) 0x18, (byte) 0x01, (byte) 0x18, (byte) 0x2D, (byte) 0x73, (byte) 0x1A, (byte) 0x96, (byte) 0x00, (byte) 0x4A,
        (byte) 0x00, (byte) 0x25, (byte) 0x80, (byte) 0x92, (byte) 0xE3, (byte) 0xB8, (byte) 0x7C, (byte) 0xC2, (byte) 0x71, (byte) 0x5C, (byte) 0x72, (byte) 0x1C,
        (byte) 0x97, (byte) 0x00, (byte) 0x4A, (byte) 0x00, (byte) 0x25, (byte) 0x80, (byte) 0x72, (byte) 0x6D, (byte) 0xB9, (byte) 0x5E, (byte) 0xD3, (byte) 0x0A,
        (byte) 0x40, (byte) 0x05, (byte) 0xA0, (byte) 0x02, (byte) 0x50, (byte) 0x31, (byte) 0x86, (byte) 0x15, (byte) 0x63, (byte) 0x58, (byte) 0x3D, (byte) 0xE0,
        (byte) 0x42, (byte) 0x52, (byte) 0x31, (byte) 0x86, (byte) 0x15, (byte) 0x63, (byte) 0x58, (byte) 0x01, (byte) 0xA8, (byte) 0x00, (byte) 0x54, (byte) 0x00,
        (byte) 0xAA, (byte) 0xF5, (byte) 0x07, (byte) 0xDE, (byte) 0x8A, (byte) 0x6A, (byte) 0x00, (byte) 0x35, (byte) 0x80, (byte) 0x1A, (byte) 0x40, (byte) 0x0D,
        (byte) 0xA0, (byte) 0x26, (byte) 0x07, (byte) 0x6A, (byte) 0x72, (byte) 0xA0, (byte) 0xBE, (byte) 0xE3, (byte) 0x4A, (byte) 0x56, (byte) 0x93, (byte) 0x03,
        (byte) 0x35, (byte) 0x39, (byte) 0x50, (byte) 0x03, (byte) 0xA8, (byte) 0x01, (byte) 0xD4, (byte) 0x00, (byte) 0x6A, (byte) 0x00, (byte) 0xF5, (byte) 0xC6,
        (byte) 0xE3, (byte) 0x7E, (byte) 0xC5, (byte) 0x63, (byte) 0x0B, (byte) 0xC0, (byte) 0x02, (byte) 0xB0, (byte) 0x00, (byte) 0x2C, (byte) 0x00, (byte) 0x0B,
        (byte) 0xC0, (byte) 0x12, (byte) 0x44, (byte) 0x96, (byte) 0x20, (byte) 0xB2, (byte) 0xE4, (byte) 0x80, (byte) 0x25, (byte) 0x07, (byte) 0x2C, (byte) 0x39,
        (byte) 0x60, (byte) 0x09, (byte) 0x22, (byte) 0x4B, (byte) 0x10, (byte) 0x59, (byte) 0x00, (byte) 0x16, (byte) 0x80, (byte) 0x05, (byte) 0x60, (byte) 0x01,
        (byte) 0x58, (byte) 0x00, (byte) 0x76, (byte) 0xF3, (byte) 0x69, (byte) 0xFB, (byte) 0x68, (byte) 0xD6, (byte) 0x00, (byte) 0x68, (byte) 0x00, (byte) 0x34,
        (byte) 0x00, (byte) 0x1A, (byte) 0x00, (byte) 0x0D, (byte) 0x80, (byte) 0x06, (byte) 0x40, (byte) 0x03, (byte) 0xA0, (byte) 0x01, (byte) 0xD0, (byte) 0x00,
        (byte) 0x68, (byte) 0x00, (byte) 0x34, (byte) 0x00, (byte) 0x1A, (byte) 0x00, (byte) 0x0D, (byte) 0x80, (byte) 0x06, (byte) 0x40, (byte) 0x03, (byte) 0xA0,
        (byte) 0xD9, (byte) 0xEA, (byte) 0xEE, (byte) 0xFD, (byte) 0xE0, (byte) 0xA4, (byte) 0x05, (byte) 0xD0, (byte) 0x02, (byte) 0x68, (byte) 0x01, (byte) 0xB4,
        (byte) 0x00, (byte) 0x5A, (byte) 0x00, (byte) 0x2D, (byte) 0x80, (byte) 0x16, (byte) 0x40, (byte) 0x0B, (byte) 0xA0, (byte) 0x05, (byte) 0xD0, (byte) 0x02,
        (byte) 0x68, (byte) 0x01, (byte) 0xB4, (byte) 0xDB, (byte) 0xCF, (byte) 0xF7, (byte) 0x86, (byte) 0x74, (byte) 0x3A, (byte) 0x06, (byte) 0x30, (byte) 0x06,
        (byte) 0x30, (byte) 0x06, (byte) 0x30, (byte) 0x06, (byte) 0x30, (byte) 0x06, (byte) 0x30, (byte) 0x06, (byte) 0x30, (byte) 0x06, (byte) 0x30, (byte) 0x7E,
        (byte) 0xC6, (byte) 0x57, (byte) 0xB3, (byte) 0x7F, (byte) 0x76, (byte) 0xFD, (byte) 0x01, (byte) 0xE2, (byte) 0x5B, (byte) 0x0D, (byte) 0x0B, (byte) 0x1B,
        (byte) 0xFF, (byte) 0x59, (byte) 0xEE, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x49, (byte) 0x45, (byte) 0x4E, (byte) 0x44, (byte) 0xAE,
        (byte) 0x42, (byte) 0x60, (byte) 0x82,
    };

    private static boolean headless() {
        String value = System.getenv("PT_HEADLESS");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    private static MemorySegment loadIcon(Arena arena) {
        MemorySegment bytes = arena.allocate(ICON_PNG.length, 1);
        MemorySegment.copy(ICON_PNG, 0, bytes, ValueLayout.JAVA_BYTE, 0, ICON_PNG.length);
        return Pt.icon_image_load_memory(bytes, ICON_PNG.length);
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.examples.icon_button.java");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x4C8DFF));

            MemorySegment window = Pt.window_create(app, "Pt Icon Button", 720, 560);
            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment eyebrow = Pt.label(root, "ICONS  ·  PNG  ·  TINT");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x4C8DFF));

            MemorySegment heading = Pt.label(root, "One image, three button slots");
            Pt.widget_set_font_size(heading, 28.0f);
            Pt.widget_set_font_weight(heading, Pt.font_weight_semibold);

            MemorySegment intro = Pt.label(root,
                    "A 32x32 PNG is decoded from memory and shared by three buttons. The button "
                    + "takes its own reference, so the example drops the load handle as soon as "
                    + "every slot is configured.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            MemorySegment card = Pt.card(root);
            Pt.widget_set_padding(card, 20.0f, 20.0f, 20.0f, 20.0f);
            Pt.widget_set_gap(card, 14.0f);

            MemorySegment image = loadIcon(arena);
            if (image.address() == 0) {
                System.err.println(Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }
            int width = Pt.icon_image_width(image);
            int height = Pt.icon_image_height(image);

            MemorySegment row = Pt.row(card);
            Pt.widget_set_gap(row, 12.0f);
            Pt.widget_set_align_self(row, Pt.align_center);

            MemorySegment untinted = Pt.button(row, "Untinted");
            Pt.button_set_variant(untinted, Pt.button_secondary);
            Pt.widget_set_tooltip(untinted, "The PNG's own warm colours");
            Pt.button_set_icon_image(untinted, image);

            MemorySegment tinted = Pt.button(row, "Tint accent");
            Pt.button_set_variant(tinted, Pt.button_primary);
            Pt.widget_set_tooltip(tinted, "Alpha mask follows the accent tint");
            Pt.button_set_icon_image(tinted, image);
            Pt.button_set_icon_tint(tinted, Pt.rgb(0x4C8DFF));

            MemorySegment iconOnly = Pt.button(row, "");
            Pt.button_set_variant(iconOnly, Pt.button_ghost);
            Pt.widget_set_a11y_name(iconOnly, "Reload icon");
            Pt.widget_set_tooltip(iconOnly, "Icon-only: the image is centred");
            Pt.button_set_icon_image(iconOnly, image);
            Pt.button_set_icon_tint(iconOnly, Pt.rgba(0x9E, 0xCE, 0x6A, 0xFF));

            /* The buttons hold their own references; share one for the window. */
            MemorySegment shared = Pt.icon_image_ref(image);
            Pt.window_set_icon(window, shared);

            MemorySegment status = Pt.label(root, "Loaded " + width + "x" + height
                    + " PNG; three buttons and the window icon hold references.");
            Pt.widget_set_font_size(status, 12.0f);
            Pt.widget_set_wrap(status, true);
            Pt.widget_set_foreground(status, Pt.rgb(0x9ECE6A));
            Pt.widget_set_a11y_name(status, "Status");

            MemorySegment tools = Pt.wrap(card);
            Pt.wrap_set_gap(tools, 8.0f, 8.0f);

            MemorySegment[] sharedSlot = {shared};
            MemorySegment[] imageSlot = {image};

            Pt.ClickHandler dropLoadHandle = (widget, data) -> {
                if (sharedSlot[0].address() == 0) {
                    Pt.widget_set_text(status, "The caller's references are already released.");
                    return;
                }
                Pt.icon_image_destroy(sharedSlot[0]);
                sharedSlot[0] = MemorySegment.NULL;
                Pt.icon_image_destroy(imageSlot[0]);
                imageSlot[0] = MemorySegment.NULL;
                Pt.widget_set_text(status, "Dropped both caller handles; slots kept "
                        + width + "x" + height + " alive.");
            };
            Pt.ClickHandler clearTint = (widget, data) -> {
                Pt.button_set_icon_tint(tinted, 0);
                Pt.widget_set_text(status, "Tint cleared: the untinted image colours return.");
            };
            Pt.ClickHandler report = (widget, data) -> {
                MemorySegment borrowed = Pt.button_get_icon_image(untinted);
                Pt.widget_set_text(status, "Tinted button still holds "
                        + (Pt.button_get_icon_image(tinted).address() != 0 ? "an image"
                                : "no image")
                        + "; untinted getter " + (borrowed.address() != 0 ? "valid" : "NULL")
                        + "; window icon "
                        + (Pt.window_get_icon(window).address() != 0 ? "retained" : "missing"));
            };

            MemorySegment dropButton = Pt.button(tools, "Drop caller handles");
            Pt.button_set_variant(dropButton, Pt.button_secondary);
            Pt.button_set_on_click(dropButton, dropLoadHandle, arena);
            MemorySegment tintButton = Pt.button(tools, "Clear tint");
            Pt.button_set_variant(tintButton, Pt.button_secondary);
            Pt.button_set_on_click(tintButton, clearTint, arena);
            MemorySegment reportButton = Pt.button(tools, "Report slots");
            Pt.button_set_variant(reportButton, Pt.button_ghost);
            Pt.button_set_on_click(reportButton, report, arena);

            MemorySegment footer = Pt.label(root,
                    "button_get_icon_image and window_get_icon return borrowed handles owned by "
                    + "the slot. Do not destroy them; destroying the slot owner drops the "
                    + "reference.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_wrap(footer, true);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            Pt.window_show(window);
            Pt.window_focus(window, untinted);

            int result;
            if (!headless()) {
                result = Pt.app_run(app);
                Pt.app_destroy(app);
                System.exit(result);
            }

            System.out.println("icon_button: loaded " + width + "x" + height
                    + " buttons=3 window_icon="
                    + (Pt.window_get_icon(window).address() != 0 ? "yes" : "no"));
            report.handle(MemorySegment.NULL, MemorySegment.NULL);
            clearTint.handle(MemorySegment.NULL, MemorySegment.NULL);
            dropLoadHandle.handle(MemorySegment.NULL, MemorySegment.NULL);
            report.handle(MemorySegment.NULL, MemorySegment.NULL);
            System.out.println("icon_button: after drop, untinted getter="
                    + (Pt.button_get_icon_image(untinted).address() != 0 ? "valid" : "NULL")
                    + " window icon="
                    + (Pt.window_get_icon(window).address() != 0 ? "retained" : "missing"));
            result = Pt.app_run_for(app, 250);
            Pt.app_destroy(app);
            System.exit(result);
        }
    }
}
