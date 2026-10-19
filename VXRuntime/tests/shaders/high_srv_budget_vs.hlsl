StructuredBuffer<float4> s0 : register(t32);
StructuredBuffer<float4> s1 : register(t33);
StructuredBuffer<float4> s2 : register(t34);
StructuredBuffer<float4> s3 : register(t35);
StructuredBuffer<float4> s4 : register(t36);
StructuredBuffer<float4> s5 : register(t37);
StructuredBuffer<float4> s6 : register(t38);
StructuredBuffer<float4> s7 : register(t39);
StructuredBuffer<float4> s8 : register(t40);
StructuredBuffer<float4> s9 : register(t41);
StructuredBuffer<float4> s10 : register(t42);
StructuredBuffer<float4> s11 : register(t43);
StructuredBuffer<float4> s12 : register(t44);
StructuredBuffer<float4> s13 : register(t45);
StructuredBuffer<float4> s14 : register(t46);
float4 main(float4 p : POSITION) : SV_Position { return p + s0[0] + s1[0] + s2[0] + s3[0] + s4[0] + s5[0] + s6[0] + s7[0] + s8[0] + s9[0] + s10[0] + s11[0] + s12[0] + s13[0] + s14[0]; }
