// Single definition source for the per-geometry GPU record.
// C++ mirror lives in Render/Binding/Geometry/GeometryData.h — keep the two identical.
#ifndef SPARK_GEOMETRY_DATA_HLSLI
#define SPARK_GEOMETRY_DATA_HLSLI

struct GeometryData
{
    uint FirstIndex;
    uint IndexCount;
    int  VertexOffset;
};

#endif // SPARK_GEOMETRY_DATA_HLSLI
