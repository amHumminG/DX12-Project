struct Camera
{
    float4 camPos;
    matrix viewProj;
};

struct RayData
{
    uint totalSpotLights;
    uint totalPointLights;
    uint frameCount;
    float pad;
};

ConstantBuffer<Camera> cameraCB : register(b0);
ConstantBuffer<RayData> rayDataCB : register(b1);

struct PixelShaderInput
{
	float4 Color    : COLOR;
};

float4 main( PixelShaderInput IN ) : SV_Target
{
    return IN.Color * cameraCB.camPos * rayDataCB.frameCount;
    return IN.Color;
}