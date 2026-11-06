// Single definition source for the per-geometry GPU record.
// C++ mirror lives in Render/Binding/Geometry/GeometryData.h — keep the two identical.
#ifndef SPARK_GEOMETRY_DATA_HLSLI
#define SPARK_GEOMETRY_DATA_HLSLI

// 64 bytes. Scalar padding preserves the same layout with std430.
struct GeometryData
{
    float3 BoundsCenter;    // of the box around the mesh's vertices, in the mesh's own space
    uint   FirstIndex;
    float3 BoundsExtents;   // half the box's size along each axis
    uint   IndexCount;
    int    VertexOffset;
    uint   IndexByteStride;  // 0: non-indexed; 2: UINT16; 4: UINT32. FirstIndex is in indices.
    uint2  BottomLevelAddress;
    uint   TriangleCount; // indexed: IndexCount / 3; non-indexed: vertex count / 3
    uint   Padding0;
    uint   Padding1;
    uint   Padding2;
};

#endif // SPARK_GEOMETRY_DATA_HLSLI
