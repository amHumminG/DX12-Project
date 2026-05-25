Texture2D<float4> colorBuffer : register(t0);
Texture2D<float4> depthGBuffer : register(t1);

#define SHADOW_EPSILON 0.0001f

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer camera : register(b0)
{
    float4 camPos;
    matrix viewProj;
}

cbuffer RayData : register(b1)
{
    uint totalSpotLights;
    uint totalPointLights;
    uint frameCount;
    float pad;
}

struct SpotLightBuffer
{
    matrix vpMatrix;
    float3 colour;
    float3 direction;
    float outAngle;
    float inAngle;
    float3 position;
    float range;
};

struct DirectionalLightBuffer
{
    float4x4 view;
    float4x4 proj;
    float3 colour;
    float3 direction;
};

struct PointLightBuffer
{
    matrix vpMatrix[6];
    float3 colour;
    float3 position;
    float range;
};

sampler shadowMapSampler : register(s0);

StructuredBuffer<SpotLightBuffer> spotLights : register(t2);
Texture2DArray<float> spotShadowMaps : register(t3);

StructuredBuffer<DirectionalLightBuffer> directionalLight : register(t4);
Texture2DArray<float> dirShadowMaps : register(t5);

StructuredBuffer<PointLightBuffer> pointLights : register(t6);
TextureCubeArray<float> pointShadowMaps : register(t7);

float PhaseHG(float cosTheta, float g)
{
    // https://omlc.org/classroom/ece532/class3/hg.html
    float g2 = g * g;
    return (1 - g2) / (2 * pow(1 + g2 - 2 * g * cosTheta, 1.5f));
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
    // https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
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

bool IsSampledPosShadowed(float3 samplePos, matrix lightViewProj, float3 sampleDir, TextureCubeArray<float> shadowMap, int index)
{
    float4 lightWorldPos = mul(float4(samplePos, 1.0f), lightViewProj);
    float calcDepth = lightWorldPos.z / lightWorldPos.w;
    
    float sampledDepth = shadowMap.SampleLevel(shadowMapSampler, float4(sampleDir, index), 0) + SHADOW_EPSILON;
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

float CalculateAttenuation(PointLightBuffer light, float3 samplePos)
{
    float3 toLight = light.position - samplePos;
    float distance = length(toLight);
    return saturate(1.0 - (distance / light.range)) * 2;
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

float4 main(PixelShaderInput IN) : SV_Target
{
    float2 uv = IN.uv;
    float4 sceneColor = colorBuffer.Sample(shadowMapSampler, uv);

    float depth = depthGBuffer.Sample(shadowMapSampler, uv).r;

    float3 worldPos = ComputeWorldSpacePosition(uv, depth, viewProj);

    float3 viewDir = worldPos - camPos.xyz;
    float viewLength = length(viewDir);
    float3 rayDir = normalize(viewDir);

    float density = 0.015f;
    const uint nSteps = 32;
    float stepSize = 2.0f;
    float maxDistance = nSteps * stepSize; 
    float noiseOffset = 2.0f;
    float4 fogColor = float4(0.5f, 0.5f, 0.5f, 1.0f);
    float scattering = 0.3f;
    float transmittance = 1.0f;

    float2 pixelCoords = IN.position.xy;

    float distLimit = min(viewLength, maxDistance);
    float distTravelled = IGN(pixelCoords,frameCount) * noiseOffset;

    for (uint step = 0; step < nSteps; step++)
    {
        if (transmittance < 0.01f || distTravelled > distLimit)
            break;

        float3 sampleWorldPos = camPos.xyz + rayDir * distTravelled;
        float3 fogSum = 0.0f;

        bool isShadowed = IsSampledPosShadowed(sampleWorldPos, mul(directionalLight[0].view, directionalLight[0].proj), dirShadowMaps, 0);
        if (density > 0.0f && !isShadowed)
        {
            float3 toLight =  normalize(-directionalLight[0].direction);
            float RdotL = dot(rayDir, toLight);
            fogSum += directionalLight[0].colour * PhaseHG(RdotL, scattering);
        }
        
        // Spot lights
        for (int i = 0; i < totalSpotLights; i++)
        {
            float3 toLight = spotLights[i].position - sampleWorldPos;
            if (dot(toLight, toLight) > spotLights[i].range * spotLights[i].range)
            {
                continue;
            }
            
            isShadowed = IsSampledPosShadowed(sampleWorldPos, spotLights[i].vpMatrix, spotShadowMaps, i);
            if (density > 0.0f && !isShadowed)
            {
                float3 toLight = normalize(spotLights[i].position - worldPos);
                float RdotL = CalculateRdotL(rayDir, toLight);
                float attenuation = abs(CalculateAttenuation(spotLights[i], sampleWorldPos));
                fogSum += spotLights[i].colour * attenuation * PhaseHG(RdotL, scattering);
            }
        }
        
        // Point lights
        for (int i = 0; i < totalPointLights; i++)
        {
            float3 toLight = pointLights[i].position - sampleWorldPos;
            if (dot(toLight, toLight) > pointLights[i].range * pointLights[i].range)
            {
                continue;
            }
            
            float3 sampleDir = normalize(sampleWorldPos - pointLights[i].position);

            // Determine cubemap face
            int face = 0;
            float3 absDir = abs(sampleDir);

            if (absDir.x > absDir.y && absDir.x > absDir.z)
                face = sampleDir.x > 0 ? 0 : 1;
            else if (absDir.y > absDir.z)
                face = sampleDir.y > 0 ? 2 : 3;
            else
                face = sampleDir.z > 0 ? 4 : 5;
            
            isShadowed = IsSampledPosShadowed(sampleWorldPos, pointLights[i].vpMatrix[face], sampleDir, pointShadowMaps, i);
            if (density > 0.0f && !isShadowed)
            {
                float3 toLight = normalize(pointLights[i].position - worldPos);
                float RdotL = CalculateRdotL(rayDir, toLight);
                float attenuation = abs(CalculateAttenuation(pointLights[i], sampleWorldPos));
                fogSum += pointLights[i].colour * attenuation * PhaseHG(RdotL, scattering);
            }
        }

        fogColor.rgb += fogSum * density * stepSize;
        transmittance *= exp(-density * stepSize);

        distTravelled += stepSize;
    }

    return lerp(sceneColor, fogColor, 1.0f - saturate(transmittance));
}