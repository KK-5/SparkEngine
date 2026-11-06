// Single definition source for the per-geometry GPU record.
// C++ mirror lives in Render/Binding/Geometry/GeometryData.h — keep the two identical.
#ifndef SPARK_GEOMETRY_DATA_HLSLI
#define SPARK_GEOMETRY_DATA_HLSLI

// 48 bytes.
struct GeometryData
{
    float3 BoundsCenter;    // of the box around the mesh's vertices, in the mesh's own space
    uint   FirstIndex;
    float3 BoundsExtents;   // half the box's size along each axis
    uint   IndexCount;
    int    VertexOffset;
    uint   IndexByteStride;  // 0: non-indexed; 2: UINT16; 4: UINT32. FirstIndex is in indices.
    uint2  BottomLevelAddress;
};

#endif // SPARK_GEOMETRY_DATA_HLSLI
