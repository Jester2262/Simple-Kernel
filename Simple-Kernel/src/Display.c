//==================================================================================================================================
//  Simple Kernel: Text and Graphics Display Output Functions
//==================================================================================================================================
//
// Version 0.8
//
// Author:
//  KNNSpeed
//
// Source Code:
//  https://github.com/KNNSpeed/Simple-Kernel
//
// This file provides various functions for text and graphics output.
//

#include "Kernel64.h"
#include "font8x8.h" // This can only be included by one file since it's h-files containing initialized variables (in this case arrays)

// Set the default font with this
#define SYSTEMFONT font8x8_basic // Must be set up in UTF-8

//----------------------------------------------------------------------------------------------------------------------------------
// Initialize_Global_Printf_Defaults: Set Up Printf
//----------------------------------------------------------------------------------------------------------------------------------
//
// Initialize printf and bind it to a specific GPU framebuffer.
//

void Initialize_Global_Printf_Defaults(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU)
{
  // Set global default print information--needed for printf
  Global_Print_Info.defaultGPU = GPU;
  Global_Print_Info.height = 8; // Character font height (height*yscale should not exceed VerticalResolution--it should still work, but it might be really messy and bizarrely cut off)
  Global_Print_Info.width = 8; // Character font width (in bits) (width*xscale should not exceed HorizontalResolution, same reason as above)
  Global_Print_Info.font_color = 0x00FFFFFF; // Default font color; GPU.Info->PixelFormat will probably always be PixelBlueGreenRedReserved8BitPerColor thanks to Windows requirements here: https://docs.microsoft.com/en-us/windows-hardware/test/hlk/testref/6afc8979-df62-4d86-8f6a-99f05bbdc7f3#more-information
  // Even if the above is not the Windows-required color order, white is still 0x00FFFFFF for PixelRedGreenBlueReserved8BitPerColor, and black is always 0.
  // PixelBltOnly is not supported, and PixelBitMask might exist for specialized things. The conditional at the end of this function handles PixelBitMask.
  Global_Print_Info.highlight_color = 0xFF000000; // Default highlight color
  Global_Print_Info.background_color = 0x00000000; // Default background color
  Global_Print_Info.x = 0; // Leftmost x-coord that's in-bounds (NOTE: per UEFI Spec 2.7 Errata A, (0,0) is always the top left in-bounds pixel.)
  Global_Print_Info.y = 0; // Topmost y-coord
  Global_Print_Info.xscale = 1; // Output width scaling factor for systemfont used by printf (positive integer scaling only, default 1 = no scaling)
  Global_Print_Info.yscale = 1; // Output height scaling factor for systemfont used by printf (positive integer scaling only, default 1 = no scaling)
  Global_Print_Info.index = 0; // Global string index for printf, etc. to keep track of cursor's postion in the framebuffer
  Global_Print_Info.textscrollmode = 0; // What to do when a newline goes off the bottom of the screen. See next comment for values.
  //
  // textscrollmode:
  //  0 = wrap around to the top (default)
  //  1 up to VerticalResolution - 1 = Scroll this many vertical lines at a time
  //                                   (NOTE: Gaps between text lines will occur
  //                                   if this is not an integer factor of the
  //                                   space below the lowest character, except
  //                                   for special cases below)
  //  VerticalResolution = Maximum supported value, will simply wipe the screen.
  //
  //  Special cases:
  //    - Using height*yscale gives a "quick scroll" for text, as it scrolls up
  //      an entire character at once (recommended mode for scrolling). This
  //      special case does not leave gaps between text lines when using
  //      arbitrary font sizes/scales.
  //    - Using VerticalResolution will just quickly wipe the screen and print
  //      the next character on top where it would have gone next anyways.
  //
  // SMOOTH TEXT SCROLLING WARNING:
  // The higher the screen resolution and the larger the font size & scaling, the
  // slower low values will be. Using 1 on a 4K screen takes almost exactly 30
  // seconds to scroll up a 120-height character on an i7-7700HQ, but, man, it's
  // smooooooth. Using 2 takes half the time, etc.
  //

  if(GPU.Info->PixelFormat == PixelBitMask) // In the event that PixelBitMask is needed, support it.
  {
    // White text
    Global_Print_Info.font_color = 0x00000000 | (GPU.Info->PixelInformation.RedMask | GPU.Info->PixelInformation.GreenMask | GPU.Info->PixelInformation.BlueMask);
    // Black highlight & background, though black is always 0, so nothing to do otherwise.
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// formatted_string_anywhere_scaled: A More Flexible Printf
//----------------------------------------------------------------------------------------------------------------------------------
//
// A massively customizable printf-like function. Supports everything printf supports and more (like scaling). Not bound to any particular GPU.
//
// height and width: height (bytes) and width (bits) of the string's font characters; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
// xscale and yscale: horizontal and vertical integer font scaling factors >= 1
// string: printf-style string
//

void formatted_string_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale, const char * string, ...)
{
  // Height in number of bytes and width in number of bits, per character where "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which is U+0040 (@). This is an 8x8 '@' sign.
  if((height > GPU.Info->VerticalResolution) || (width > GPU.Info->HorizontalResolution)) // Need some kind of error indicator (makes screen red)
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  } // Could use an instruction like ARM's USAT to truncate values
  else if((x > GPU.Info->HorizontalResolution) || (y > GPU.Info->VerticalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.GreenMask);
    }
    else
    {
      Colorscreen(GPU, 0x0000FF00); // Makes screen green
    }
  }
  else if (((y + yscale*height) > GPU.Info->VerticalResolution) || ((x + xscale*width) > GPU.Info->HorizontalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.BlueMask);
    }
    else
    {
      Colorscreen(GPU, 0x000000FF); // Makes screen blue
    }
  }

  va_list arguments;

  va_start(arguments, string);
  ssize_t buffersize = vsnprintf(NULL, 0, string, arguments); // Get size of needed buffer, though (v)snprintf does not account for \0
  void * output_string = malloc(buffersize + 1); // Haha, yes! :) Realistically this'll only ever need 1 UEFI page at a time. But hey, now the string size is basically unlimited!
  vsprintf((char*)output_string, string, arguments); // Write string to buffer
  va_end(arguments);

  string_anywhere_scaled(GPU, output_string, height, width, font_color, highlight_color, x, y, xscale, yscale);

  free(output_string); // Free malloc'd memory
}

//----------------------------------------------------------------------------------------------------------------------------------
// Resetdefaultscreen: Reset Printf Cursor and Black Screen
//----------------------------------------------------------------------------------------------------------------------------------
//
// Reset Printf cursor to (0,0) and wipe the visible portion of the screen buffer to black
//

void Resetdefaultscreen(void)
{
  Global_Print_Info.x = 0;
  Global_Print_Info.y = 0;
  Global_Print_Info.index = 0;
  Blackscreen(Global_Print_Info.defaultGPU);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Resetdefaultcolorscreen: Reset Printf Cursor and Color Screen
//----------------------------------------------------------------------------------------------------------------------------------
//
// Reset Printf cursor to (0,0) and wipe the visible portiion of the screen buffer area to the default background color (whatever is
// currently set as Global_Print_Info.background_color)
//

void Resetdefaultcolorscreen(void)
{
  Global_Print_Info.x = 0;
  Global_Print_Info.y = 0;
  Global_Print_Info.index = 0;
  Colorscreen(Global_Print_Info.defaultGPU, Global_Print_Info.background_color);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Blackscreen: Make the Screen Black
//----------------------------------------------------------------------------------------------------------------------------------
//
// Wipe the visible portion of the screen buffer to black
//

void Blackscreen(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU)
{
  Colorscreen(GPU, 0x00000000);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Colorscreen: Make the Screen a Color
//----------------------------------------------------------------------------------------------------------------------------------
//
// Wipe the visible portion of the screen buffer to a specified color
//

void Colorscreen(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 color)
{
  Global_Print_Info.background_color = color;

  AVX_memset_4B((EFI_PHYSICAL_ADDRESS*)GPU.FrameBufferBase, color, GPU.Info->VerticalResolution * GPU.Info->PixelsPerScanLine);
/*  // This could work, too, if writing to the offscreen area is undesired. It'll probably be a little slower than a contiguous AVX_memset_4B, however.
  for (row = 0; row < GPU.Info->VerticalResolution; row++)
  {
    // Per UEFI Spec 2.7 Errata A, framebuffer address 0 coincides with the top leftmost pixel. i.e. screen padding is only HorizontalResolution + porch.
    AVX_memset_4B((EFI_PHYSICAL_ADDRESS*)(GPU.FrameBufferBase + 4 * GPU.Info->PixelsPerScanLine * row), color, GPU.Info->HorizontalResolution); // The thing at FrameBufferBase is an address pointing to UINT32s. FrameBufferBase itself is a 64-bit number.
  }
*/

/* // Old version (non-AVX)
  UINT32 row, col;
  UINT32 backporch = GPU.Info->PixelsPerScanLine - GPU.Info->HorizontalResolution; // The area offscreen is the back porch. Sometimes it's 0.

  for (row = 0; row < GPU.Info->VerticalResolution; row++)
  {
    for (col = 0; col < (GPU.Info->PixelsPerScanLine - backporch); col++) // Per UEFI Spec 2.7 Errata A, framebuffer address 0 coincides with the top leftmost pixel. i.e. screen padding is only HorizontalResolution + porch.
    {
      *(UINT32*)(GPU.FrameBufferBase + 4 * (GPU.Info->PixelsPerScanLine * row + col)) = color; // The thing at FrameBufferBase is an address pointing to UINT32s. FrameBufferBase itself is a 64-bit number.
    }
  }
*/

/* // Leaving this here for posterity. The framebuffer size might be a fair bit larger than the visible area (possibly for scrolling support? Regardless, some are just the size of the native resolution).
  for(UINTN i = 0; i < GPU.FrameBufferSize; i+=4) //32 bpp == 4 Bpp
  {
    *(UINT32*)(GPU.FrameBufferBase + i) = color; // FrameBufferBase is a 64-bit address that points to UINT32s.
  }
*/
}

//----------------------------------------------------------------------------------------------------------------------------------
// single_pixel: Color a Single Pixel
//----------------------------------------------------------------------------------------------------------------------------------
//
// Set a specified pixel, in (x,y) coordinates from the top left of the screen (0,0), to a specified color
//

// Screen turns red if a pixel is put outside the visible area.
void single_pixel(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 x, UINT32 y, UINT32 color)
{
  if((y > GPU.Info->VerticalResolution) || (x > GPU.Info->HorizontalResolution)) // Need some kind of error indicator (makes screen red)
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  }

  *(UINT32*)(GPU.FrameBufferBase + (y * GPU.Info->PixelsPerScanLine + x) * 4) = color;
//  Output_render(GPU, 0x01, 1, 1, color, 0xFF000000, x, y, 1, 1, 0); // Make highlight transparent to skip that part of output render (transparent = no highlight)
}

//----------------------------------------------------------------------------------------------------------------------------------
// single_char: Color a Single Character
//----------------------------------------------------------------------------------------------------------------------------------
//
// Print a single character of the default font (set at the top of this file) at the top left of the screen (0,0) and at a specified
// font color and highlight color
//
// character: 'a' or 'b' (with single quotes), for example
// height and width: height (bytes) and width (bits) of the font character; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
//

void single_char(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color)
{
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which would be U+0040 (@) -- an 8x8 '@' sign.
  if((height > GPU.Info->VerticalResolution) || (width > GPU.Info->HorizontalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  } // Could use an instruction like ARM's USAT to truncate values

  Output_render_text(GPU, character, height, width, font_color, highlight_color, 0, 0, 1, 1, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// single_char_anywhere: Color a Single Character Anywhere
//----------------------------------------------------------------------------------------------------------------------------------
//
// Print a single character of the default font (set at the top of this file), at (x,y) coordinates from the top left of the screen
// (0,0), using a specified font color and highlight color
//
// character: 'a' or 'b' (with single quotes), for example
// height and width: height (bytes) and width (bits) of the font character; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
//

void single_char_anywhere(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y)
{
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which would be U+0040 (@) -- an 8x8 '@' sign.
  if((height > GPU.Info->VerticalResolution) || (width > GPU.Info->HorizontalResolution)) // Need some kind of error indicator (makes screen red)
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  } // Could use an instruction like ARM's USAT to truncate values
  else if((x > GPU.Info->HorizontalResolution) || (y > GPU.Info->VerticalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.GreenMask);
    }
    else
    {
      Colorscreen(GPU, 0x0000FF00); // Makes screen green
    }
  }
  else if (((y + height) > GPU.Info->VerticalResolution) || ((x + width) > GPU.Info->HorizontalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.BlueMask);
    }
    else
    {
      Colorscreen(GPU, 0x000000FF); // Makes screen blue
    }
  }

  Output_render_text(GPU, character, height, width, font_color, highlight_color, x, y, 1, 1, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// single_char_anywhere_scaled: Color a Single Character Anywhere with Scaling
//----------------------------------------------------------------------------------------------------------------------------------
//
// Print a single character of the default font (set at the top of this file), at (x,y) coordinates from the top left of the screen
// (0,0), using a specified font color, highlight color, and scale factors
//
// character: 'a' or 'b' (with single quotes), for example
// height and width: height (bytes) and width (bits) of the font character; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
// xscale and yscale: horizontal and vertical integer font scaling factors >= 1
//

void single_char_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale)
{
  // Assuming "character" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which would be U+0040 (@) -- an 8x8 '@' sign.
  if((height > GPU.Info->VerticalResolution) || (width > GPU.Info->HorizontalResolution)) // Need some kind of error indicator (makes screen red)
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  } // Could use an instruction like ARM's USAT to truncate values
  else if((x > GPU.Info->HorizontalResolution) || (y > GPU.Info->VerticalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.GreenMask);
    }
    else
    {
      Colorscreen(GPU, 0x0000FF00); // Makes screen green
    }
  }
  else if (((y + yscale*height) > GPU.Info->VerticalResolution) || ((x + xscale*width) > GPU.Info->HorizontalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.BlueMask);
    }
    else
    {
      Colorscreen(GPU, 0x000000FF); // Makes screen blue
    }
  }

  Output_render_text(GPU, character, height, width, font_color, highlight_color, x, y, xscale, yscale, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// string_anywhere_scaled: Color a String Anywhere with Scaling
//----------------------------------------------------------------------------------------------------------------------------------
//
// Print a string of the default font (set at the top of this file), at (x,y) coordinates from the top left of the screen
// (0,0), using a specified font color, highlight color, and scale factors
//
// string: "a string like this" (no formatting specifiers -- use formatted_string_anywhere_scaled() for that instead)
// height and width: height (bytes) and width (bits) of the string's font characters; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
// xscale and yscale: horizontal and vertical integer font scaling factors >= 1
//
// NOTE: literal strings in C are automatically null-terminated. i.e. "hi" is actually 3 characters long.
// This function allows direct output of a pre-made string, either a hardcoded one or one made via sprintf.
//

void string_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const char * string, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale)
{
  if((height > GPU.Info->VerticalResolution) || (width > GPU.Info->HorizontalResolution)) // Need some kind of error indicator (makes screen red)
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  } // Could use an instruction like ARM's USAT to truncate values
  else if((x > GPU.Info->HorizontalResolution) || (y > GPU.Info->VerticalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.GreenMask);
    }
    else
    {
      Colorscreen(GPU, 0x0000FF00); // Makes screen green
    }
  }
  else if (((y + yscale*height) > GPU.Info->VerticalResolution) || ((x + xscale*width) > GPU.Info->HorizontalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.BlueMask);
    }
    else
    {
      Colorscreen(GPU, 0x000000FF); // Makes screen blue
    }
  }

  //mapping: x*4 + y*4*(PixelsPerScanLine), x is column number, y is row number; every 4*PixelsPerScanLine bytes is a new row
  // y max is VerticalResolution, x max is HorizontalResolution, 'x4' is the LFB expansion, since 1 bit input maps to 4 bytes output

  // A 2x scale should turn 1 pixel into a square of 2x2 pixels
// iterate through characters in string
// start while
  uint32_t index = 0;
  while(string[index] != '\0') // for char in string until \0
  {
    // match the character to the font, using UTF-8.
    // This would expect a font array to include everything in the UTF-8 character set... Or just use the most common ones.
    Output_render_text(GPU, string[index], height, width, font_color, highlight_color, x, y, xscale, yscale, index);
    index++;
  } // end while
} // end function

//----------------------------------------------------------------------------------------------------------------------------------
// Output_render_text: Render a Character to the Screen
//----------------------------------------------------------------------------------------------------------------------------------
//
// This function draws a character of the default font on the screen, given the following parameters:
//
// character: 'a' or 'b' (with single quotes), for example
// height and width: height (bytes) and width (bits) of the font character; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
// xscale and yscale: horizontal and vertical integer font scaling factors >= 1
// index: mainly for strings, it's for keeping track of which character in the string is being output
//

void Output_render_text(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, int character, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale, UINT32 index)
{
  Output_render_bitmap(GPU, SYSTEMFONT[character], height, width, font_color, highlight_color, x, y, xscale, yscale, index);
  // Can also make a 'draw filled-in scaled rectangle' function now...
}


//----------------------------------------------------------------------------------------------------------------------------------
// bitmap_anywhere_scaled: Color a Single Bitmap Anywhere with Scaling
//----------------------------------------------------------------------------------------------------------------------------------
//
// Print a single, single-color bitmapped character at (x,y) coordinates from the top left of the screen (0,0) using a specified font
// color, highlight color, and scale factors
//
// bitmap: a bitmapped image formatted like a font character
// height and width: height (bytes) and width (bits) of the bitmap; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
// xscale and yscale: horizontal and vertical integer scaling factors >= 1
//
// This function allows for printing of individual characters not in the default font, making it like single_char_anywhere, but for
// non-font characters and similarly-formatted images. Just pass in an appropriately-formatted array of bytes as the "bitmap" pointer.
//
// Note that single_char_anywhere_scaled() takes 'a' or 'b', this would take something like character_array['a'] instead.
//

void bitmap_anywhere_scaled(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const unsigned char * bitmap, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale)
{
  // Assuming "bitmap" is an array of bytes, e.g. { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, which would be U+0040 (@) -- an 8x8 '@' sign.
  if((height > GPU.Info->VerticalResolution) || (width > GPU.Info->HorizontalResolution)) // Need some kind of error indicator (makes screen red)
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.RedMask);
    }
    else
    {
      Colorscreen(GPU, 0x00FF0000); // Makes screen red
    }
  } // Could use an instruction like ARM's USAT to truncate values
  else if((x > GPU.Info->HorizontalResolution) || (y > GPU.Info->VerticalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.GreenMask);
    }
    else
    {
      Colorscreen(GPU, 0x0000FF00); // Makes screen green
    }
  }
  else if (((y + yscale*height) > GPU.Info->VerticalResolution) || ((x + xscale*width) > GPU.Info->HorizontalResolution))
  {
    if(GPU.Info->PixelFormat == PixelBitMask)
    {
      Colorscreen(GPU, GPU.Info->PixelInformation.BlueMask);
    }
    else
    {
      Colorscreen(GPU, 0x000000FF); // Makes screen blue
    }
  }

  Output_render_bitmap(GPU, bitmap, height, width, font_color, highlight_color, x, y, xscale, yscale, 0);
}


//----------------------------------------------------------------------------------------------------------------------------------
// Output_render_bitmap: Render a Single-Color Bitmap to the Screen
//----------------------------------------------------------------------------------------------------------------------------------
//
// This function draws a bitmapped character, given the following parameters:
//
// bitmap: a bitmapped image formatted like a font character
// height and width: height (bytes) and width (bits) of the bitmap; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
// font_color: font color
// highlight_color: highlight/background color for the string's characters (it's called highlight color in word processors)
// x and y: coordinate positions of the top leftmost pixel of the string
// xscale and yscale: horizontal and vertical integer scaling factors >= 1
// index: mainly for strings, it's for keeping track of which character in the string is being output
//
// This is essentially the same thing as Output_render_text(), but for bitmaps that are not part of the default font.
//
// Note that single_char_anywhere_scaled() takes 'a' or 'b', this would take something like character_array['a'] instead.
//

// GCC has an issue where it will sign-extend a value being used with bsf/tzcnt... See discussion at: https://stackoverflow.com/questions/48634422/can-i-get-rid-of-a-sign-extend-between-ctz-and-addition-to-a-pointer
// This function is basically the same as __builtin_ctz(), but
static inline uint32_t output_render_ctz_32(uint32_t input)
{
  uint32_t output;

#ifdef __AVX2__
  asm volatile("tzcntl %[in], %[out]"
              : [out] "=a" (output)
              : [in] "b" (input)
              : // No clobbers
              );
#else
  asm volatile("bsfl %[in], %[out]"
              : [out] "=a" (output)
              : [in] "b" (input)
              : // No clobbers
              );
#endif

  return output;
}

// "Unwrapped" Version
void Output_render_bitmap(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const unsigned char * bitmap, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale, UINT32 index)
{
  // Compact ceiling function, so that size doesn't need to be passed in
  uint32_t row_iterator = (width >> 3); // How many bytes are in a row
  if(width & 0x7)
  {
    row_iterator++;
  }
  uint32_t last_byte = row_iterator - 1;
  uint32_t last_byte_valid_bits = width - (last_byte * 8);
  uint8_t width_remainder_mask = ~(0xFF << last_byte_valid_bits); // Only want the low 8 bits of this result for the mask

  uint32_t current_char_row = 0;
  // Width should never be zero, so the iterator will always be at least 1
  uint32_t transparency_color = 0xFF000000;
  if(GPU.Info->PixelFormat == PixelBitMask)
  {
    transparency_color = GPU.Info->PixelInformation.ReservedMask;
  }
  uint64_t BytesPerScanline = GPU.Info->PixelsPerScanLine * 4;
  uint64_t PixelByteScale = xscale * 4;
  EFI_PHYSICAL_ADDRESS pixel_address_base = GPU.FrameBufferBase + ( (y * GPU.Info->PixelsPerScanLine + x + xscale * index * width) * 4 ); // Group the constant terms
  // EFI_PHYSICAL_ADDRESS is a uint64_t
  uint64_t pixel_row = pixel_address_base - (yscale * BytesPerScanline);

  if( !((font_color | highlight_color) & transparency_color) ) // Neither font nor highlight are transparent
  {
    // font and highlight output

    for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
    {
      pixel_row += yscale * BytesPerScanline;

      for(uint32_t byte = 0; byte < last_byte; byte++) // for bit in column
      {
        // If a 1, output font, otherwise background
        uint8_t char_byte = bitmap[current_char_row + byte]; // Used to find contiguous zeroes
        uint32_t inverse_char_byte = ~((uint32_t)char_byte); // Needed to find contiguous ones

        uint64_t pixel_column = (PixelByteScale * 8 * byte) + pixel_row;
        uint32_t prev_count = 0;

        while(char_byte) // Alternate font_color components with highlight_color components
        { // Thankfully, tzcnt and bsf output the same values when input is guaranteed to be nonzero
          uint32_t tz_count = __builtin_ctz((uint32_t)char_byte); // Counts contiguous zeroes
          uint32_t tz_count_diff = tz_count - prev_count; // So we don't overwrite anything already drawn
          prev_count = tz_count; // Keep track of the number of contiguous bits for contiguous ones

          while(tz_count_diff)
          {
            // start scale here
            for(uint32_t b = 0; b < yscale; b++)
            {
              uint64_t scale_row = (BytesPerScanline * b) + pixel_column;

              for(uint32_t a = 0; a < xscale; a++)
              {
                uint64_t scale_column = (4 * a) + scale_row;
                *(UINT32*)scale_column = highlight_color;
              }
            } //end scale here

            tz_count_diff--;
            inverse_char_byte ^= (inverse_char_byte & -inverse_char_byte); // Erase the bit just output
            pixel_column += PixelByteScale;
          }

          // That's a set of contiguous zeroes, now do a set of contiguous ones in the byte...

          tz_count = __builtin_ctz(inverse_char_byte); // Counts contiguous zeroes, which in the inverse case are actually contiguous ones
          // The earlier typecast of inverse_char_byte puts a 'wall' of ones so that this doesn't continue until the end, in tzcnt's case, or undefined behavior, in bsf's case
          tz_count_diff = tz_count - prev_count; // So we don't overwrite anything already drawn
          prev_count = tz_count; // Keep track of the number of contiguous bits for the next set of contiguous zeroes

          while(tz_count_diff)
          {
            // start scale here
            for(uint32_t b = 0; b < yscale; b++)
            {
              uint64_t scale_row = (BytesPerScanline * b) + pixel_column;

              for(uint32_t a = 0; a < xscale; a++)
              {
                uint64_t scale_column = (4 * a) + scale_row;
                *(UINT32*)scale_column = font_color;
              }
            } //end scale here

            tz_count_diff--;
            char_byte ^= (char_byte & -char_byte); // Erase the bit just output
            pixel_column += PixelByteScale;
          }
        } // end while char_byte has font components in it
        // Whatever's left of the byte is to be contiguous highlight_color, otherwise it would have been taken care of in the previous loop
        while(8 - prev_count)
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = (BytesPerScanline * b) + pixel_column;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = (4 * a) + scale_row;
              *(UINT32*)scale_column = highlight_color;
            }
          } //end scale here

          pixel_column += PixelByteScale;
          prev_count++;
        }
      } // end bit in column

      // Last byte may have a weird size
      uint8_t last_char_byte = bitmap[current_char_row + last_byte] & width_remainder_mask; // Used to find contiguous zeroes
      uint32_t inverse_last_char_byte = ~((uint32_t)last_char_byte); // Needed to find contiguous ones

      uint64_t last_pixel_column = (PixelByteScale * 8 * last_byte) + pixel_row;
      uint32_t last_prev_count = 0;

      while(last_char_byte) // Alternate font_color components with highlight_color components
      { // Thankfully, tzcnt and bsf output the same values when input is guaranteed to be nonzero
        uint32_t tz_count = __builtin_ctz((uint32_t)last_char_byte); // Counts contiguous zeroes
        uint32_t tz_count_diff = tz_count - last_prev_count; // So we don't overwrite anything already drawn
        last_prev_count = tz_count; // Keep track of the number of contiguous bits for contiguous ones

        while(tz_count_diff)
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = (BytesPerScanline * b) + last_pixel_column;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = (4 * a) + scale_row;
              *(UINT32*)scale_column = highlight_color;
            }
          } //end scale here

          tz_count_diff--;
          inverse_last_char_byte ^= (inverse_last_char_byte & -inverse_last_char_byte); // Erase the bit just output
          last_pixel_column += PixelByteScale;
        }

        // That's a set of contiguous zeroes, now do a set of contiguous ones in the byte...

        tz_count = __builtin_ctz(inverse_last_char_byte); // Counts contiguous zeroes, which in the inverse case are actually contiguous ones
        // The earlier typecast of inverse_char_byte puts a 'wall' of ones so that this doesn't continue until the end, in tzcnt's case, or undefined behavior, in bsf's case
        tz_count_diff = tz_count - last_prev_count; // So we don't overwrite anything already drawn
        last_prev_count = tz_count; // Keep track of the number of contiguous bits for the next set of contiguous zeroes

        while(tz_count_diff)
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = (BytesPerScanline * b) + last_pixel_column;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = (4 * a) + scale_row;
              *(UINT32*)scale_column = font_color;
            }
          } //end scale here

          tz_count_diff--;
          last_char_byte ^= (last_char_byte & -last_char_byte); // Erase the bit just output
          last_pixel_column += PixelByteScale;
        }
      } // end while char_byte has font components in it
      // Whatever's left of the byte is to be contiguous highlight_color, otherwise it would have been taken care of in the previous loop
      // This one could be a weird size, however
      while(last_byte_valid_bits - last_prev_count)
      {
        // start scale here
        for(uint32_t b = 0; b < yscale; b++)
        {
          uint64_t scale_row = (BytesPerScanline * b) + last_pixel_column;

          for(uint32_t a = 0; a < xscale; a++)
          {
            uint64_t scale_column = (4 * a) + scale_row;
            *(UINT32*)scale_column = highlight_color;
          }
        } //end scale here

        last_pixel_column += PixelByteScale;
        last_prev_count++;
      }

      current_char_row += row_iterator;
    } // end byte in row

  } // end condition
  else if( (!(font_color & transparency_color)) && (highlight_color & transparency_color) ) // highlight is transparent, font is not
  {
    // No highlight output

    for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
    {
      pixel_row += yscale * BytesPerScanline;

      for(uint32_t byte = 0; byte < last_byte; byte++) // for bit in column
      {
        uint8_t char_byte = bitmap[current_char_row + byte];

        while(char_byte)
        { // Thankfully, tzcnt and bsf output the same values when input is guaranteed to be nonzero
          uint32_t tz_count = __builtin_ctz((uint32_t)char_byte);
          uint64_t pixel_column = (PixelByteScale * ((8 * byte) + tz_count)) + pixel_row; // Can skip all the zeroes. Also (8*byte + tz_count) is actually base-8 math.

          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = (b * BytesPerScanline) + pixel_column;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = (a * 4) + scale_row;
              *(UINT32*)scale_column = font_color;
            }
          } //end scale here

          char_byte ^= (char_byte & -char_byte); // Don't need the least significant 1 bit anymore
        } //end while
        // if a 0, do nothing for highlight (transparency)
      } // end bit in column

      // Last byte may have a weird size
      uint8_t last_char_byte = bitmap[current_char_row + last_byte] & width_remainder_mask; // Want to invert it for highlight_color's ctz. This makes all 0s into 1s, so tzcnt and bsf still work for 'no font, yes background'.

      while(last_char_byte)
      { // Thankfully, tzcnt and bsf output the same values when input is guaranteed to be nonzero
        uint32_t tz_count = __builtin_ctz((uint32_t)last_char_byte);
        uint64_t last_pixel_column = (PixelByteScale * ((8 * last_byte) + tz_count)) + pixel_row; // Can skip all the zeroes. Also (8*byte + tz_count) is actually base-8 math.

        // start scale here
        for(uint32_t b = 0; b < yscale; b++)
        {
          uint64_t scale_row = (b * BytesPerScanline) + last_pixel_column;

          for(uint32_t a = 0; a < xscale; a++)
          {
            uint64_t scale_column = (a * 4) + scale_row;
            *(UINT32*)scale_column = font_color;
          }
        } //end scale here

        last_char_byte ^= (last_char_byte & -last_char_byte); // Don't need the least significant 1 bit anymore
      } //end while

      current_char_row += row_iterator;
    } // end byte in row

  } // end condition
  else if( (font_color & transparency_color) && (!(highlight_color & transparency_color)) ) // font is transparent, highlight is not
  {
    // No font output

    for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
    {
      pixel_row += yscale * BytesPerScanline;

      for(uint32_t byte = 0; byte < last_byte; byte++) // for bit in column
      {
        uint8_t char_byte = ~(bitmap[current_char_row + byte]); // Want to invert it for highlight_color's ctz. This makes all 0s into 1s, so tzcnt and bsf still work for 'no font, yes background'.

        while(char_byte)
        { // Thankfully, tzcnt and bsf output the same values when input is guaranteed to be nonzero
          // This does not need to have the upper 24 bits set to 1 (which case #1's inverse needs) because the while loop checks for if char_byte has anything in it, which prevents this from running after the last 1 bit has been cleared.
          // Case #1 does not have the same assurance when the "contiguous ones" portion runs on the inverse of char_byte there.
          uint32_t tz_count = __builtin_ctz((uint32_t)char_byte);
          uint64_t pixel_column = (PixelByteScale * ((8 * byte) + tz_count)) + pixel_row; // Can skip all the zeroes (err... ones). Also (8*byte + tz_count) is actually base-8 math.

          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = (b * BytesPerScanline) + pixel_column;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = (a * 4) + scale_row;
              *(UINT32*)scale_column = highlight_color;
            }
          } //end scale here

          char_byte ^= (char_byte & -char_byte); // Don't need the least significant 1 bit anymore
        } //end while
        // if a 0, do nothing for highlight (transparency)
      } // end bit in column

      // Last byte may have a weird size
      uint8_t last_char_byte = ~(bitmap[current_char_row + last_byte]) & width_remainder_mask; // Want to invert it for highlight_color's ctz. This makes all 0s into 1s, so tzcnt and bsf still work for 'no font, yes background'.

      while(last_char_byte)
      { // Thankfully, tzcnt and bsf output the same values when input is guaranteed to be nonzero
        uint32_t tz_count = __builtin_ctz((uint32_t)last_char_byte);
        uint64_t last_pixel_column = (PixelByteScale * ((8 * last_byte) + tz_count)) + pixel_row; // Can skip all the zeroes (err... ones). Also (8*byte + tz_count) is actually base-8 math.

        // start scale here
        for(uint32_t b = 0; b < yscale; b++)
        {
          uint64_t scale_row = (b * BytesPerScanline) + last_pixel_column;

          for(uint32_t a = 0; a < xscale; a++)
          {
            uint64_t scale_column = (a * 4) + scale_row;
            *(UINT32*)scale_column = highlight_color;
          }
        } //end scale here

        last_char_byte ^= (last_char_byte & -last_char_byte); // Don't need the least significant 1 bit anymore
      } //end while

      current_char_row += row_iterator;
    } // end byte in row

  } // end condition
  // else both font and highlight are transparent, do nothing.
  // Each pixel of output is ultimately being computed like this: *(UINT32*)(GPU.FrameBufferBase + ((y*GPU.Info->PixelsPerScanLine + x) + yscale*row*GPU.Info->PixelsPerScanLine + xscale*bit + (b*GPU.Info->PixelsPerScanLine + a) + xscale * index * width)*4) = font/highlight_color;
}

//
// Extra bitmap rendering method notes:
//
// The way this used to be done is with 4x for loops and a branch, like this:
//
//  Input part:        Scale part:
// for -> for -> if -> for -> for
//               else -> for -> for
//
// This is really slow for larger sizes, as it needs to perform a conditoinal branch on every single bit of the input bitmap.
// For an 8x8 font, that's 64 conditional branches. This is what is referred to in the below "old section," as it was written
// before I went optimization-crazy on it. The output methodology changed when I came across this link, which talked about the
// "count trailing zeros" instruction:
// https://lemire.me/blog/2018/02/21/iterating-over-set-bits-quickly/
//
// This got the optimization senses tingling, as this instruction presents a way to remove the conditional branches entirely
// with a 1-3 cycle instruction (per document #4, "Instruction tables," of https://www.agner.org/optimize/) and allow the
// renderer to spend much more time scaling and outputting each bit in a contiguous set of bits without needing to check every
// single bit. This implementation also retains the ability to do weirdly-sized bitmap fonts and graphics (i.e. width's not a
// multiple of 8). In the worst-case scenario of this function, which would be a byte like 0b01010101, it should not execute
// any slower than the worst-case scenario of a conditional branch per bit with a missed prediction.
//
// It turns out that the theory behind optimizing branches like this has been documented since 1995:
// http://www.cs.fsu.edu/~whalley/papers/pldi95.pdf ("Avoiding Conditional Branches by Code Replication", by F. Mueller & D. Whalley)
// To quote the paper:
//
// "A number of programs beyond the set in Table 3
// were tested and it was found that some conditional
// branches could be avoided in every one of these programs.
// Yet, about 1/3 of the programs resulted in an execution
// benefit of 1% or less. It was also observed that the
// effectiveness of avoiding conditional branches is highly data
// dependent. If branches are avoided in loops with high
// execution frequencies, then the benefits can be quite high.
// This observation would suggest that avoiding conditional
// branches could be selectively applied where profiling data
// indicates that high benefits are more likely."
//
// Which is particularly interesting because it coincides exactly with what was observed here.
//
// After profiling it with get_tick() on an Intel i7-7700HQ (Kaby Lake family) and GCC optimization -Og, the "unwrapped" renderer
// above is only marginally faster than the old "conditional branch" method for a mixed load. It is consistently marginally faster,
// to be fair, but all that for only about 0.06%-3% speedup. "Mixed load" meaning printing a big bitmap and a lot of random text
// to stress the ctz instruction.
//
// NOTE: 0.06% is about 50 million cycles for every 65.4 billion cycles. It really depends on the load, though. Worst-case it's
// about the same as the conditional branch version, but best-case it ought to be much faster (e.g. sparse bitmaps with contiguous
// bits grouped together). That stated, I have not explicitly tested any best-case scenarios (like a bunch of spaces and
// transparencies), as the idea was to be faster in the worst cases. A transparent-highlight space run through the unwrapped method
// will definitely eclipse the conditonal branch method.
//
// Update: Simply changing the default system highlight color to 'transparent' netted a performance gain of up to 15-25% for the
// unwrapped method over the conditional branch method. Using transparencies was also 2-3x faster overall for both methods.
//
// ***OLD SECTION*** (division method vs. conditional branch method)
// This also works...
/*
  uint64_t pixel_row = pixel_address_base - BytesPerScanline;

  for(uint32_t row = 0; row < (yscale*height); row++) // for number of rows in the output character
  {
    pixel_row += BytesPerScanline;

    for(uint32_t bit = 0; bit < (xscale*width); bit++) // for bit in column
    {
      pixel_column = pixel_row + 4 * bit;

      // If a 1, output font, otherwise background
      if((bitmap[(row/yscale)*row_iterator + ((bit/xscale) >> 3)] >> ((bit/xscale) & 0x7)) & 0x1)
      {
        *(UINT32*)pixel_column = font_color;
      }
      else
      {
        *(UINT32*)pixel_column = highlight_color;
      }
    } // end bit in column
  } // end byte in row
*/
// But notice that there are 3 divisions (well, actually only 2, since (bit/xscale) occurs twice). Though this
// method produces smaller code, it can be a LOT slower than using 4x for loops and a conditional branch. Each
// division can take something like 20-100 cycles (see document #4, "Instruction tables," of
// https://www.agner.org/optimize/), and that's because dvision is generally implemented like this:
/*
  if D = 0 then error(DivisionByZeroException) end
  Q := 0                  -- Initialize quotient and remainder to zero
  R := 0
  for i := n − 1 .. 0 do  -- Where n is number of bits in N
    R := R << 1           -- Left-shift R by 1 bit
    R(0) := N(i)          -- Set the least-significant bit of R equal to bit i of the numerator
    if R ≥ D then
      R := R − D
      Q(i) := 1
    end
  end

 Source: https://en.wikipedia.org/wiki/Division_algorithm#Integer_division_(unsigned)_with_remainder
*/
// So this technique would require doing that TWICE for EVERY SINGLE PIXEL of output. That's potentially
// hundreds of cycles just doing division! It also bottlenecks the CPU because nearby code can't be processed
// out-of-order while dividing, which arises from the fact that the next set of instructions entirely depends
// on the result of the division.
//
// By contrast, using 4x for loops with no division only has a couple extra jumps and compares, which can be
// micro-op fused and branch-predicted (and these jumps are not nearly as expensive as division--not even close).
// Also, the conditional that checks for a 0 or 1 in the source bitmap, which really has a 50-50 chance of
// either outcome, only needs to be run once per source bit instead of for every single output pixel.
//
// The 4x loop method is easily parallelizable, as well. In fact, graphics processors do something similar to
// stitch frames together after their thousands of cores have worked on different parts of frames (it's how
// render output units, or ROPs, assemble frames before writing to the display framebuffer for output. See
// https://developer.nvidia.com/content/life-triangle-nvidias-logical-pipeline). Graphics cards usually use an
// intermediary buffer before output, however, while here the CPU just cuts that out and draws straight to the
// screen's framebuffer. The 4x loop method is also similar to using 4x identical monitors in a 2x2 grid of
// monitors in order to get one "virtual montior" of 4x higher resolution.
//
/*
// OLD BITMAP RENDERER BELOW
// "Conditional Branch" Version
void Output_render_bitmap(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, const unsigned char * bitmap, UINT32 height, UINT32 width, UINT32 font_color, UINT32 highlight_color, UINT32 x, UINT32 y, UINT32 xscale, UINT32 yscale, UINT32 index)
{
  // Compact ceiling function, so that size doesn't need to be passed in
  uint32_t row_iterator = (width >> 3); // How many bytes are in a row
  if(width & 0x7)
  {
    row_iterator++;
  }
  uint32_t current_char_row = 0;
  // Width should never be zero, so the iterator will always be at least 1
  uint32_t transparency_color = 0xFF000000;
  if(GPU.Info->PixelFormat == PixelBitMask)
  {
    transparency_color = GPU.Info->PixelInformation.ReservedMask;
  }
  uint64_t BytesPerScanline = GPU.Info->PixelsPerScanLine * 4;
  uint64_t PixelByteScale = xscale * 4;
  EFI_PHYSICAL_ADDRESS pixel_address_base = GPU.FrameBufferBase + ( (y * GPU.Info->PixelsPerScanLine + x + xscale * index * width) * 4 ); // Group the constant terms
  // EFI_PHYSICAL_ADDRESS is a uint64_t
  uint64_t pixel_row = pixel_address_base - (yscale * BytesPerScanline);

  if(!((font_color | highlight_color) & transparency_color)) // Neither font nor highlight are transparent
  {
    for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
    {
      pixel_row += yscale * BytesPerScanline;

      for(uint32_t bit = 0; bit < width; bit++) // for bit in column
      {
        uint64_t pixel_column = pixel_row + PixelByteScale * bit;

        // If a 1, output font, otherwise background
        // (bit >> 3) is the byte index: integer divide by 8 to get index. Saves an entire branch.
        if((bitmap[current_char_row + (bit >> 3)] >> (bit & 0x7)) & 0x1)
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = pixel_column + b * BytesPerScanline;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = scale_row + a * 4;
              *(UINT32*)scale_column = font_color;
            }
          } // end scale here
        } // end if
        // if a 0, background
        else
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = pixel_column + b * BytesPerScanline;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = scale_row + a * 4;
              *(UINT32*)scale_column = highlight_color;
            }
          } // end scale here
        } // end else
      } // end bit in column

      current_char_row += row_iterator;
    } // end byte in row
  }
  else if( (!(font_color & transparency_color)) && (highlight_color & transparency_color) ) // highlight is transparent, font is not
  {
    // No highlight output, still need branch
    for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
    {
      pixel_row += yscale * BytesPerScanline;

      for(uint32_t bit = 0; bit < width; bit++) // for bit in column
      {
        uint64_t pixel_column = pixel_row + PixelByteScale * bit;

        // If a 1, output font, otherwise background
        // (bit >> 3) is the byte index: integer divide by 8 to get index. Saves an entire branch.
        if((bitmap[current_char_row + (bit >> 3)] >> (bit & 0x7)) & 0x1)
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = pixel_column + b * BytesPerScanline;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = scale_row + a * 4;
              *(UINT32*)scale_column = font_color;
            }
          } // end scale here
        } // end if
        // if a 0, do nothing for highlight (transparency)
      } // end bit in column

      current_char_row += row_iterator;
    } // end byte in row
  }
  else if( (font_color & transparency_color) && (!(highlight_color & transparency_color)) ) // font is transparent, highlight is not
  {
    // No font output, still need branch
    for(uint32_t row = 0; row < height; row++) // for number of rows in the character of the fontarray
    {
      pixel_row += yscale * BytesPerScanline;

      for(uint32_t bit = 0; bit < width; bit++) // for bit in column
      {
        uint64_t pixel_column = pixel_row + PixelByteScale * bit;

        // If not a 1, output background
        // (bit >> 3) is the byte index: integer divide by 8 to get index. Saves an entire branch.
        if(!((bitmap[current_char_row + (bit >> 3)] >> (bit & 0x7)) & 0x1))
        {
          // start scale here
          for(uint32_t b = 0; b < yscale; b++)
          {
            uint64_t scale_row = pixel_column + b * BytesPerScanline;

            for(uint32_t a = 0; a < xscale; a++)
            {
              uint64_t scale_column = scale_row + a * 4;
              *(UINT32*)scale_column = highlight_color;
            }
          } // end scale here
        } // end if
        // if a 1, do nothing for font (transparency)
      } // end bit in column

      current_char_row += row_iterator;
    } // end byte in row
  }
  // else both font and highlight are transparent, do nothing.
  // Each pixel of output is ultimately being computed like this: *(UINT32*)(GPU.FrameBufferBase + ((y*GPU.Info->PixelsPerScanLine + x) + yscale*row*GPU.Info->PixelsPerScanLine + xscale*bit + (b*GPU.Info->PixelsPerScanLine + a) + xscale * index * width)*4) = font/highlight_color;
}
*/
//
// AVX NOTE: All bitmap renderers above could be sped up with AVX (namely with preloaded 256-bit vectors of font color & highlight color, then instead of outputting 1 pixel at scale_column output 8 pixels with storeu).
// However this would mean that scale factors would be constrained to multiples of 8, and the minimum scale factor would be 8 instead of 1.
// Similarly, an entire row of an 8x8 font can fit into a 256-bit vector (using blendv between 2x preloaded font_color and highligh_color vectors), but then you can't scale it without using
// extract_epi32 -> set1_epi32/broadcast (costs several extra cycles over the preload->store8px method and limits scale factors to multiples of 8 again) or a temporary array (costs memory accesses).
//
// So, while AVX is nice and all, using it gives up a lot of flexibility, as either scale factors get constrained to multiples of 8 (fast) or scaling can't be used at all and fonts/bitmaps are
// constrained to be multiples of 8 in width (fastest).
//

/*
void Output_render_vector(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GPU, UINT32 x_init, UINT32 y_init, UINT32 x_final, UINT32 y_final, UINT32 color, UINT32 xscale, UINT32 yscale)
{

}
*/

//----------------------------------------------------------------------------------------------------------------------------------
// bitmap_bitswap: Swap Bitmap Bits
//----------------------------------------------------------------------------------------------------------------------------------
//
// Swaps the high 4 bits with the low 4 bits in each byte of an array
//
// bitmap: an array of bytes
// height and width: height (bytes) and width (bits) of the bitmap; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
//

void bitmap_bitswap(const unsigned char * bitmap, UINT32 height, UINT32 width, unsigned char * output)
{
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1

  for(uint32_t iter = 0; iter < height*row_iterator; iter++) // Flip one byte at a time
  {
    output[iter] = (((bitmap[iter] >> 4) & 0xF) | ((bitmap[iter] & 0xF) << 4));
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// bitmap_bitreverse: Reverse Bitmap Bits
//----------------------------------------------------------------------------------------------------------------------------------
//
// Inverts each individual byte in an array: 01234567 --> 76543210
// It reverses the order of bits in each byte of an array, but it does not reorder any bytes. This does not change endianness, as
// changing endianness would be reversing the order of bytes in a given data type like uint64_t.
//
// bitmap: an array of bytes
// height and width: height (bytes) and width (bits) of the bitmap; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
//

void bitmap_bitreverse(const unsigned char * bitmap, UINT32 height, UINT32 width, unsigned char * output)
{
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1

  for(uint32_t iter = 0; iter < height*row_iterator; iter++) // Invert one byte at a time
  {
    output[iter] = 0;
    for(uint32_t bit = 0; bit < 8; bit++)
    {
      if( bitmap[iter] & (1 << (7 - bit)) )
      {
        output[iter] += (1 << bit);
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// bitmap_bytemirror: Mirror a Rectangular Array of Bytes
//----------------------------------------------------------------------------------------------------------------------------------
//
// Requires rectangular arrays, and it creates a horizontal reflection of the entire array (almost like looking in a mirror)
// Does not reverse the bits, hence the "almost." Run bitmap_bitreverse on the output of this to do that part.
//
// bitmap: a rectangular array of bytes
// height and width: height (bytes) and width (bits) of the bitmap; there is no automatic way of getting this information for weird font sizes (e.g. 17 bits wide), sorry.
//

void bitmap_bytemirror(const unsigned char * bitmap, UINT32 height, UINT32 width, unsigned char * output) // Width in bits, height in bytes
{
  uint32_t row_iterator = width >> 3;
  if((width & 0x7) != 0)
  {
    row_iterator++;
  } // Width should never be zero, so the iterator will always be at least 1

  uint32_t iter, parallel_iter;
  if(row_iterator & 0x01)
  {// Odd number of bytes per row
    for(iter = 0, parallel_iter = row_iterator - 1; (iter + (row_iterator >> 1) + 1) < height*row_iterator; iter++, parallel_iter--) // Mirror one byte at a time
    {
      if(iter == parallel_iter) // Integer divide, (iter%row_iterator == row_iterator/2 - 1)
      {
        iter += (row_iterator >> 1) + 1; // Hop the middle byte
        parallel_iter = iter + row_iterator - 1;
      }

      output[iter] = bitmap[parallel_iter]; // parallel_iter must mirror iter
      output[parallel_iter] = bitmap[iter];
    }
  }
  else
  {// Even number of bytes per row
    for(iter = 0, parallel_iter = row_iterator - 1; (iter + (row_iterator >> 1)) < height*row_iterator; iter++, parallel_iter--) // Mirror one byte at a time
    {
      if(iter - 1 == parallel_iter) // Integer divide, (iter%row_iterator == row_iterator/2 - 1)
      {
        iter += (row_iterator >> 1); // Skip to the next row to swap
        parallel_iter = iter + row_iterator - 1; // Appropriately position parallel_iter based on iter
      }

      output[iter] = bitmap[parallel_iter]; // Parallel_iter must mirror iter
      output[parallel_iter] = bitmap[iter];
    }
  }
}
