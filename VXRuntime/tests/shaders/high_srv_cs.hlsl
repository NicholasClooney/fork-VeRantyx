StructuredBuffer<uint> data : register(t33);
ByteAddressBuffer raw : register(t64);
StructuredBuffer<uint> last : register(t127);
cbuffer Indices : register(b0) { uint4 idx; };
cbuffer Constants : register(b9) { uint4 constants[32]; };
RWStructuredBuffer<uint> result : register(u0);
[numthreads(1,1,1)]
void main() {
    uint n, stride, bytes; data.GetDimensions(n, stride); raw.GetDimensions(bytes);
    result[0] = data[0]; result[1] = data[1]; result[2] = data[n];
    result[3] = raw.Load(0); result[4] = raw.Load(4); result[5] = raw.Load(bytes);
    result[6] = n; result[7] = stride; result[8] = bytes;
    result[9] = constants[idx.x].x; result[10] = constants[idx.y].x;
    result[11] = last[1]; result[12] = last[2];
}
