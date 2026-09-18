/* The two header fields Wine-NX build 108 (Test Build 2) refuses in Condemned.exe,
 * both making "map target" fail with STATUS_INVALID_IMAGE_FORMAT (c000007b).
 * tools/fix_condemned_exe.py does the same on a computer.
 *
 * 1. SizeOfImage not a multiple of the section alignment: the repack's exe had
 *    SecuROM's sections cut off without rounding. Windows rounds it up; build 108
 *    judges the last section too large (fixed upstream in build 109).
 * 2. A writable shared section (IMAGE_SCN_MEM_SHARED, Monolith's ".SHARED"). Wine
 *    maps those from a shared file, which the Horizon server never provides. With
 *    one program at a time on the Switch a private section behaves the same.
 *
 * Works on the first bytes of the file (the headers); returns how many fields it
 * changed, or -1 when they are not the headers of a 32-bit PE. Shared with a test
 * on the computer, so it only uses the types below. */
#define PE_FIX_SCN_MEM_SHARED 0x10000000u
#define PE_FIX_SCN_MEM_WRITE  0x80000000u

static unsigned int pe_fix_u32( const unsigned char *p )
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

static void pe_fix_put_u32( unsigned char *p, unsigned int v )
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static int pe_fix_headers( unsigned char *h, unsigned int size )
{
    unsigned int pe, opt, align, image, fixed, first, count, i;
    int changes = 0;

    if (size < 0x40 || h[0] != 'M' || h[1] != 'Z') return -1;
    pe = pe_fix_u32( h + 0x3c );
    if (pe > size - 0xf8 || h[pe] != 'P' || h[pe + 1] != 'E' || h[pe + 2] || h[pe + 3]) return -1;
    if ((h[pe + 24] | h[pe + 25] << 8) != 0x10b) return -1;   /* PE32 optional header */
    opt = pe + 24;
    count = h[pe + 6] | h[pe + 7] << 8;
    first = opt + (h[pe + 20] | h[pe + 21] << 8);
    if (first + count * 40 > size) return -1;

    align = pe_fix_u32( h + opt + 32 );
    if (align < 0x1000) align = 0x1000;
    image = pe_fix_u32( h + opt + 56 );
    fixed = (image + align - 1) & ~(align - 1);
    if (fixed != image)
    {
        pe_fix_put_u32( h + opt + 56, fixed );
        changes++;
    }
    for (i = 0; i < count; i++)
    {
        unsigned char *field = h + first + i * 40 + 36;
        unsigned int chars = pe_fix_u32( field );

        if ((chars & PE_FIX_SCN_MEM_SHARED) && (chars & PE_FIX_SCN_MEM_WRITE))
        {
            pe_fix_put_u32( field, chars & ~PE_FIX_SCN_MEM_SHARED );
            changes++;
        }
    }
    return changes;
}
