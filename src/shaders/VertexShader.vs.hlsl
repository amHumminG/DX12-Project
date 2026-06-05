cbuffer PerObject : register(b0)
{
    float4x4 model;
}

cbuffer PerFrame : register(b1)
{
    float4x4 view;
    float4x4 proj;
}

struct VertexPosColor
{
    float3 Position : POSITION;
    float3 Color    : COLOR;
};

struct VertexShaderOutput
{
	float4 Color    : COLOR;
    float4 Position : SV_Position;
};

VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
    OUT.Position = mul(mul(float4(IN.Position, 1.0f), model), mul(view, proj));
    OUT.Color = float4(IN.Color, 1.0f);

    return OUT;
}