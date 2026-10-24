// Copies a reserved buffer word for word into a plain one, so its pages are read through a
// shader view: mapped pages give what was copied in, unmapped ones zero.

ByteAddressBuffer   g_Reserved : register(t0, space0);
RWByteAddressBuffer g_Output   : register(u0, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    g_Output.Store(id.x * 4, g_Reserved.Load(id.x * 4));
}
