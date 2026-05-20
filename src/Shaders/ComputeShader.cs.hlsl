//cbuffer Data : register(b0)
//{
//	matrix InverseView;
//	matrix InverseProjection;
//	float3 CameraPosition;
//	float3 LightDirection;
//}

cbuffer CameraData : register(b0)
{
	matrix MVP;
}

RWTexture2D<float4> RenderTarget : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;

    uint width, height;
    RenderTarget.GetDimensions(width, height);
    if (pixelCoord.x >= width || pixelCoord.y >= height) {
        return;
    }


    float r = (float)pixelCoord.x / width;
    float g = (float)pixelCoord.y / height;
    float b = 0.5f;

    RenderTarget[pixelCoord] = float4(r, g, b, 1.0f);
}
