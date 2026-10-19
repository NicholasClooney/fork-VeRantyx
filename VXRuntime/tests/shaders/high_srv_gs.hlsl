StructuredBuffer<float4> data : register(t127);
struct V { float4 pos : SV_Position; float4 color : COLOR; };
[maxvertexcount(3)]
void main(triangle V input[3], inout TriangleStream<V> stream) {
    for (uint i = 0; i < 3; ++i) { V o = input[i]; o.color += data[0]; stream.Append(o); }
}
