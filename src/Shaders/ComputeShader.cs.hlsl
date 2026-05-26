RWTexture2D<float4> ComputeOutput : register(u0);

cbuffer CameraData : register(b0)
{
    float4 camPos;
	matrix viewProj;
}

cbuffer RayData : register(b1)
{
    int raySteps;
    int frameCount;
    float2 pad;
}

struct DirectionalLightBuffer
{
    matrix vpMatrix;
    float3 color;
    float pad1;
    float3 direction;
    float pad2;
};

StructuredBuffer<DirectionalLightBuffer> DirLight : register(t4);
Texture2DArray<float> DirLightShadowMap : register(t5);

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> DepthBuffer : register(t1);
SamplerState Sampler : register(s0);

//[numthreads(8, 8, 1)]
//void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
//    uint2 pixelCoord = dispatchThreadID.xy;
//
//    uint width, height;
//    SceneColor.GetDimensions(width, height);
//    if (pixelCoord.x >= width || pixelCoord.y >= height) {
//        return;
//    }
//
//    ComputeOutput[pixelCoord] = SceneColor[pixelCoord];
//}

float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1 - g2) / (2 * pow(1 + g2 - 2 * g * cosTheta, 3.0f / 2.0f));
}

float3 ComputeWorldSpacePosition(float2 uv, float depth, matrix vp)
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    float4 clipPos = float4(ndc, depth, 1.0f);

    float4 worldPos = mul(clipPos, vp);
    worldPos /= worldPos.w;

    return worldPos.xyz;
}

float IGN(float2 pixel, int frame)
{
    frame = frame % 64;
    float x = float(pixel.x) + 5.588238f * float(frame);
    float y = float(pixel.y) + 5.588238f * float(frame);
    return fmod(52.9829189f * fmod(0.06711056f * float(x) + 0.00583715f * float(y), 1.0f), 1.0f);
}

bool IsSampledPosShadowed(float3 samplePos, matrix lightViewProj, Texture2DArray<float> shadowMap, int index)
{
    float4 lightWorldPos = mul(float4(samplePos, 1.0f), lightViewProj);
    float2 ndcSpace = lightWorldPos.xy / lightWorldPos.w;
    float calcDepth = lightWorldPos.z / lightWorldPos.w;

    // If the samplePos is outside the light shadow map
    if (lightWorldPos.w <= 0.0f)
        return false;
    if (abs(ndcSpace.x) > 1.0f || abs(ndcSpace.y) > 1.0f)
        return false;

    float3 shadowMapUV = float3(ndcSpace.x * 0.5f + 0.5f, ndcSpace.y * -0.5f + 0.5f, index);
    float sampledDepth = shadowMap.SampleLevel(shadowMapSampler, shadowMapUV, 0) + SHADOW_EPSILON;
    return sampledDepth < calcDepth;
}

float CalculateAttenuation(SpotLightBuffer light, float3 samplePos)
{
    float3 toLight = normalize(light.position - samplePos);
    float dotCone = -dot(toLight, normalize(light.direction));
    float coneAngle = acos(dotCone); // The angle from direction vector to toLight vector.
    if (coneAngle < light.inAngle) // If inside inner cone, fully lit
    {
        return 1.0f;
    }
    else if (coneAngle < light.outAngle) // If outside innercone but inside outer cone, attenuate
    {
        float coneAttenuation = (coneAngle - light.inAngle) / (light.outAngle - light.inAngle);
        coneAttenuation = -(coneAttenuation - 1.0f);
        return coneAttenuation;
    }
    return 0.0f; // Outside light cone
}

float CalculateRdotL(float3 rayDir, float3 toLight)
{
    return dot(rayDir, toLight);
}

float3 NormalizeByMaxComponent(float3 v)
{
    float m = max(max(abs(v.x), abs(v.y)), abs(v.z));
    return (m > 0) ? v / m : v;
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 resolution;
    backBufferUAV.GetDimensions(resolution.x, resolution.y);
    float2 uv = (DTid.xy + 0.5f) / resolution;
    float4 col = backBufferUAV[DTid.xy];

    float depth = depthGBuffer.Load(int3(DTid.xy, 0));

    float3 worldPos = ComputeWorldSpacePosition(uv, depth, viewProj);

    float3 viewDir = worldPos;
    float viewLength = length(viewDir);
    float3 rayDir = normalize(viewDir);

    // Volumetric fog settings
    float density = 0.04f;
    const uint nSteps = 32;
    float stepSize = 2.0f;
    float maxDistance = nSteps * stepSize;
    float noiseOffset = 2.0f;
    float4 fogColor = 0.2f;
    float scattering = 0.3f;
    float transmittance = 1.0f;

    float2 pixelCoords = DTid.xy;
    float distLimit = min(viewLength, maxDistance);
    float distTravelled = IGN(pixelCoords, frameCount) * noiseOffset;

    // Ray-marching
    for (uint step = 0; step < nSteps; step++)
    {
        if (transmittance < 0.01f || distTravelled > distLimit)
            break;

        float3 sampleWorldPos = camPos.xyz + rayDir * distTravelled;
        float3 fogSum = 0.0f;

        // Directional light
        bool isShadowed = IsSampledPosShadowed(sampleWorldPos, directionalLight.vpMatrix, dirShadowMaps, 0);
        if (density > 0.0f && !isShadowed)
        {
            float3 toLight = normalize(-directionalLight[0].direction);
            float RdotL = CalculateRdotL(rayDir, toLight);
            fogSum += directionalLight[0].color * PhaseHG(RdotL, scattering);
        }

        fogColor.rgb += fogSum * density * stepSize;
        transmittance *= exp(-density * stepSize);

        distTravelled += stepSize;
    }

    backBufferUAV[DTid.xy] = lerp(col, fogColor, 1.0f - saturate(transmittance));
}
