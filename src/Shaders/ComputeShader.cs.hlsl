RWTexture2D<float4> ComputeOutput : register(u0);

cbuffer CameraData : register(b0)
{
    float3 camPos;
    float camPad;
	matrix inverseVP;
}

cbuffer RayData : register(b1)
{
    int raySteps;
    int frameCount;
    float2 rayPad;
}

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> DepthBuffer : register(t1);

struct DirectionalLight
{
    matrix view;
    matrix proj;
    float3 color;
    float dirPad1;
    float3 direction;
    float dirPad2;
};

StructuredBuffer<DirectionalLight> DirLight : register(t4);
Texture2DArray<float> DirLightShadowMap : register(t5);

SamplerState ShadowSampler : register(s0);

#define SHADOW_EPSILON 0.0001f

// Henyey-Greenstein Phase Function
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1 - g2) / (2 * pow(1 + g2 - 2 * g * cosTheta, 3.0f / 2.0f));
}

float3 ComputeWorldSpacePosition(float2 uv, float depth, matrix inverseVP)
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    float4 clipPos = float4(ndc, depth, 1.0f);

    float4 worldPos = mul(clipPos, inverseVP);
    worldPos /= worldPos.w;

    return worldPos.xyz;
}

// Interleaved Gradient Noise
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
    float sampledDepth = shadowMap.SampleLevel(ShadowSampler, shadowMapUV, 0) + SHADOW_EPSILON;
    return sampledDepth < calcDepth;
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 resolution;
    SceneColor.GetDimensions(resolution.x, resolution.y);
    if (DTid.x >= resolution.x || DTid.y >= resolution.y) return;

    float2 pixelCoords = DTid.xy;
    float2 uv = (pixelCoords + 0.5f) / resolution;

    float4 col = SceneColor[pixelCoords];
    float depth = DepthBuffer.Load(int3(pixelCoords, 0));

    float3 worldPos = ComputeWorldSpacePosition(uv, depth, inverseVP);
    float3 viewDir = worldPos - camPos;
    float viewLength = length(viewDir);
    float3 rayDir = normalize(viewDir);

    // Volumetric fog settings
    float density = 0.04f;
    float maxDistance = 64.0f;
    float stepSize = maxDistance / max(float(raySteps), 1.0f);
    float noiseOffset = 2.0f;
    float3 fogColor = float3(0.01f, 0.01f, 0.2f);
    float scattering = 0.3f;
    float transmittance = 1.0f;

    float distLimit = min(viewLength, maxDistance);
    float distTravelled = IGN(pixelCoords, frameCount) * noiseOffset;

    DirectionalLight dirLight = DirLight[0];
    float3 toLight = normalize(-dirLight.direction);
    float3 fogSum = float3(0.0f, 0.0f, 0.0f);
    float phase = PhaseHG(dot(rayDir, toLight), scattering);

    // Ray-marching
    for (uint step = 0; step < raySteps; step++)
    {
        if (transmittance < 0.01f || distTravelled > distLimit) break;

        float3 sampleWorldPos = camPos.xyz + (rayDir * distTravelled);
        float3 currentStepFog = float3(0.0f, 0.0f, 0.0f);

        bool isShadowed = IsSampledPosShadowed(sampleWorldPos, mul(dirLight.view, dirLight.proj), DirLightShadowMap, 0);
        if (density > 0.0f && !isShadowed) currentStepFog += dirLight.color * phase;

        fogSum += currentStepFog * density * stepSize;
        transmittance *= exp(-density * stepSize);

        distTravelled += stepSize;
    }

    ComputeOutput[pixelCoords] = float4(lerp(col.rgb, fogColor + fogSum, 1.0f - saturate(transmittance)), 1.0f);
}
