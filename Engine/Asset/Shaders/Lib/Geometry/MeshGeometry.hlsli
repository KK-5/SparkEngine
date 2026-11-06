// Standard mesh attributes from the engine geometry pool. No ray query or material evaluation.
#ifndef SPARK_LIB_MESH_GEOMETRY_HLSLI
#define SPARK_LIB_MESH_GEOMETRY_HLSLI

#include <Shaders/GeometryBindings.hlsli>

// Resource::StandardVertex: object-space position/normal/tangent and UV0, 48 bytes.
struct MeshVertex
{
    float3 Position;
    float3 Normal;
    float4 Tangent;
    float2 TexCoord0;
};

struct MeshTriangle
{
    MeshVertex Vertex0;
    MeshVertex Vertex1;
    MeshVertex Vertex2;
};

MeshVertex LoadMeshVertex(uint vertexIndex)
{
    const uint address = vertexIndex * 48u;
    MeshVertex vertex;
    vertex.Position = asfloat(g_MeshGeometry.Load3(address));
    vertex.Normal = asfloat(g_MeshGeometry.Load3(address + 12u));
    vertex.Tangent = asfloat(g_MeshGeometry.Load4(address + 24u));
    vertex.TexCoord0 = asfloat(g_MeshGeometry.Load2(address + 40u));
    return vertex;
}

// ByteAddressBuffer.Load requires a 4-byte-aligned address. Round down to the word
// containing this byte, preserving indices that start in its upper 16 bits.
uint AlignDownTo4Bytes(uint byteOffset)
{
    return byteOffset & ~3u;
}

// byteOffsetInWord is 0 or 2: select the lower or upper UINT16 of a loaded 32-bit word.
uint ExtractUint16FromWord(uint word, uint byteOffsetInWord)
{
    const uint bitOffset = byteOffsetInWord * 8u;
    return (word >> bitOffset) & 0xffffu;
}

// Returns a pool-global vertex index from a geometry-local index/vertex element.
uint LoadMeshVertexIndex(GeometryData geometry, uint localIndex)
{
    uint index = localIndex;
    if (geometry.IndexByteStride != 0)
    {
        const uint address = (geometry.FirstIndex + localIndex) * geometry.IndexByteStride;
        const uint alignedAddress = AlignDownTo4Bytes(address);
        const uint word = g_MeshGeometry.Load(alignedAddress);
        const uint byteOffsetInWord = address - alignedAddress;
        index = geometry.IndexByteStride == 2u ? ExtractUint16FromWord(word, byteOffsetInWord) : word;
    }
    return uint(geometry.VertexOffset) + index;
}

uint GetMeshTriangleCount(GeometryData geometry)
{
    return geometry.TriangleCount;
}

// triangleIndex is local to this mesh primitive, matching ray query's PrimitiveIndex.
// Precondition: triangleIndex < GetMeshTriangleCount(geometry).
MeshTriangle LoadMeshTriangle(GeometryData geometry, uint triangleIndex)
{
    const uint first = triangleIndex * 3u;
    MeshTriangle meshTriangle;
    meshTriangle.Vertex0 = LoadMeshVertex(LoadMeshVertexIndex(geometry, first));
    meshTriangle.Vertex1 = LoadMeshVertex(LoadMeshVertexIndex(geometry, first + 1u));
    meshTriangle.Vertex2 = LoadMeshVertex(LoadMeshVertexIndex(geometry, first + 2u));
    return meshTriangle;
}

// Same convention as ray query: barycentrics are the weights of vertices 1 and 2.
// Valid surface points have x >= 0, y >= 0, x + y <= 1; weights are not clamped.
float3 GetTriangleBarycentricWeights(float2 barycentrics)
{
    return float3(1.0 - barycentrics.x - barycentrics.y, barycentrics);
}

#endif // SPARK_LIB_MESH_GEOMETRY_HLSLI
