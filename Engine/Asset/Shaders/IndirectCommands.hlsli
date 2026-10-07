// The records an indirect call reads from its argument buffer, for a shader that writes
// them. C++ mirror lives in RHI/Command/IndirectCommands.h — keep the two identical.
#ifndef SPARK_INDIRECT_COMMANDS_HLSLI
#define SPARK_INDIRECT_COMMANDS_HLSLI

struct DrawIndexedIndirectCommand
{
    uint IndexCount;
    uint InstanceCount;
    uint FirstIndex;
    int  VertexOffset;
    uint FirstInstance;
};

#endif // SPARK_INDIRECT_COMMANDS_HLSLI
