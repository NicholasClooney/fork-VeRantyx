ByteAddressBuffer data : register(t127);
float4 main(float4 pos : SV_Position, float4 color : COLOR) : SV_Target {
    uint bytes; data.GetDimensions(bytes);
    return color + float4(data.Load(0), data.Load(4), data.Load(bytes), bytes == 8 ? 0 : 1) / 255.0;
}
