#pragma once

#include <RHI/Origin.h>
#include <RHI/Size.h>
#include <RHI/Resource/Image/Image.h>

namespace Spark::RHI
{
    //! Maps a box of tiles of one mip of one array slice of a reserved image onto the memory of a
    //! tile backing, or unmaps it. The box is in tiles of Image::GetTileShape(); its tiles take
    //! the backing's tiles from m_backingTileOffset on, x first, then y, then z.
    struct ImageTileMapping
    {
        const Image* m_image = nullptr;
        uint16_t     m_mipSlice = 0;
        uint16_t     m_arraySlice = 0;
        Origin       m_tileOrigin;
        Size         m_tileExtent;

        //! Null unmaps the box. Else an image of ImageBindFlags::TileBacking, of the reserved image's
        //! format and, the flag aside, its bind flags. Its tiles are counted along its rows, then
        //! its array slices.
        const Image* m_backingImage = nullptr;
        uint32_t     m_backingTileOffset = 0;
    };
}
