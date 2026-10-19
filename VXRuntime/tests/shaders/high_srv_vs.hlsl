cbuffer A : register(b0) { float4 a; };
cbuffer B : register(b1) { float4 b; };
StructuredBuffer<float4> first : register(t33);
StructuredBuffer<float4> second : register(t38);
struct Out { float4 pos : SV_Position; float4 color : COLOR; };
Out main(float2 pos : POSITION) {
    Out o; o.pos = float4(pos, 0, 1);
    uint n, stride; first.GetDimensions(n, stride);
    o.color = first[0] + second[1] + a + b;
    o.color += first[n] + second[2];
    o.color.w = (n == 2 && stride == 16) ? 1 : 0;
    return o;
}
